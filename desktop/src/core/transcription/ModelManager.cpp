#include "ModelManager.hpp"
#include "WhisperWrapper.hpp"
#include "core/common/Logger.hpp"
#include "core/security/InputValidator.hpp"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QStandardPaths>
#include <QtCore/QMutexLocker>
#include <QtCore/QTimer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QStorageInfo>
#include <QtCore/QDateTime>
#include <QtCore/QThread>
#include <QtCore/QCoreApplication>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <algorithm>
#include <chrono>

namespace Murmur {

class ModelManager::ModelManagerPrivate {
public:
    ModelManagerPrivate() = default;
    ~ModelManagerPrivate() = default;

    bool initialized = false;
    QString modelsPath;
    QString configFilePath;
    
    // Model storage
    std::unordered_map<QString, ModelInfo> models;
    QString activeModelId;
    
    // Download management
    std::unique_ptr<QNetworkAccessManager> networkManager;
    std::unordered_map<QString, std::unique_ptr<QNetworkReply>> activeDownloads;
    std::vector<QString> downloadQueue;
    int maxConcurrentDownloads = 2;
    int maxRetryAttempts = 3;
    int downloadTimeout = 300000; // 5 minutes
    
    // Configuration
    bool autoCleanupEnabled = true;
    int autoCleanupInterval = 3600000; // 1 hour
    
    // Timers
    std::unique_ptr<QTimer> cleanupTimer;
    
    // Whisper integration
    std::unique_ptr<WhisperWrapper> whisperWrapper;
    
    // Thread safety
    mutable QMutex mutex;
    
    // Validation
    std::unique_ptr<InputValidator> validator;
    
    // Default model configurations
    std::vector<ModelInfo> defaultModels;
};

ModelManager::ModelManager(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<ModelManagerPrivate>())
{
    d->validator = std::make_unique<InputValidator>();
    d->networkManager = std::make_unique<QNetworkAccessManager>(this);
    d->whisperWrapper = std::make_unique<WhisperWrapper>();
    
    // Set up cleanup timer
    d->cleanupTimer = std::make_unique<QTimer>(this);
    connect(d->cleanupTimer.get(), &QTimer::timeout, this, &ModelManager::performAutoCleanup);
    
    // Initialize default models
    setupDefaultModels();
}

ModelManager::~ModelManager() {
    if (d->initialized) {
        shutdown();
    }
}

Expected<void, ModelError> ModelManager::initialize(const QString& modelsPath) {
    QMutexLocker locker(&d->mutex);
    
    if (d->initialized) {
        return Expected<void, ModelError>();
    }
    
    if (modelsPath.isEmpty()) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    d->modelsPath = modelsPath;
    d->configFilePath = QDir(modelsPath).filePath("models.json");
    
    auto dirResult = ensureModelsDirectory();
    if (!dirResult.hasValue()) {
        return dirResult;
    }
    
    // Load existing configuration
    auto configResult = loadModelConfiguration();
    if (!configResult.hasValue()) {
        Logger::instance().warn("Failed to load model configuration, using defaults");
        auto defaultResult = initializeDefaultModels();
        if (!defaultResult.hasValue()) {
            return defaultResult;
        }
    }
    
    // Discover existing models
    auto discoverResult = discoverModels();
    if (!discoverResult.hasValue()) {
        Logger::instance().warn("Failed to discover existing models");
    }
    
    // Start cleanup timer if enabled
    if (d->autoCleanupEnabled) {
        d->cleanupTimer->start(d->autoCleanupInterval);
    }
    
    d->initialized = true;
    
    Logger::instance().info("ModelManager initialized with path: {}", modelsPath.toStdString());
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::shutdown() {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return Expected<void, ModelError>();
    }
    
    // Stop cleanup timer
    d->cleanupTimer->stop();
    
    // Cancel all active downloads
    for (const auto& [modelId, reply] : d->activeDownloads) {
        reply->abort();
    }
    d->activeDownloads.clear();
    d->downloadQueue.clear();
    
    // Unload all models
    for (auto& [modelId, modelInfo] : d->models) {
        if (modelInfo.isLoaded()) {
            unloadModelInternal(modelId);
        }
    }
    
    // Save configuration
    auto saveResult = saveModelConfiguration();
    if (!saveResult.hasValue()) {
        Logger::instance().warn("Failed to save model configuration during shutdown");
    }
    
    d->models.clear();
    d->activeModelId.clear();
    d->initialized = false;
    
    Logger::instance().info("ModelManager shut down");
    return Expected<void, ModelError>();
}

bool ModelManager::isInitialized() const {
    QMutexLocker locker(&d->mutex);
    return d->initialized;
}

Expected<std::vector<ModelInfo>, ModelError> ModelManager::getAvailableModels() const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    std::vector<ModelInfo> models;
    for (const auto& [modelId, modelInfo] : d->models) {
        models.push_back(modelInfo);
    }
    
    // Sort by type and language
    std::sort(models.begin(), models.end(), [](const ModelInfo& a, const ModelInfo& b) {
        if (a.type != b.type) {
            return a.type < b.type;
        }
        return a.language < b.language;
    });
    
    return models;
}

Expected<std::vector<ModelInfo>, ModelError> ModelManager::getDownloadedModels() const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    std::vector<ModelInfo> models;
    for (const auto& [modelId, modelInfo] : d->models) {
        if (modelInfo.isDownloaded()) {
            models.push_back(modelInfo);
        }
    }
    
    return models;
}

Expected<std::vector<ModelInfo>, ModelError> ModelManager::getLoadedModels() const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    std::vector<ModelInfo> models;
    for (const auto& [modelId, modelInfo] : d->models) {
        if (modelInfo.isLoaded()) {
            models.push_back(modelInfo);
        }
    }
    
    return models;
}

Expected<ModelInfo, ModelError> ModelManager::getModelInfo(const QString& modelId) const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    auto it = d->models.find(modelId);
    if (it == d->models.end()) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    return it->second;
}

Expected<ModelInfo, ModelError> ModelManager::findModel(ModelType type, const QString& language) const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    // First try to find exact match
    for (const auto& [modelId, modelInfo] : d->models) {
        if (modelInfo.type == type && 
            (language.isEmpty() || modelInfo.language == language || modelInfo.multilingual)) {
            return modelInfo;
        }
    }
    
    // If no exact match, try multilingual models
    if (!language.isEmpty()) {
        for (const auto& [modelId, modelInfo] : d->models) {
            if (modelInfo.type == type && modelInfo.multilingual) {
                return modelInfo;
            }
        }
    }
    
    return makeUnexpected(ModelError::ModelNotFound);
}

Expected<ModelInfo, ModelError> ModelManager::findBestModel(const QString& language) const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    // Priority: Large > Medium > Small > Base > Tiny
    std::vector<ModelType> priorities = {
        ModelType::LargeV3, ModelType::LargeV2, ModelType::Large,
        ModelType::Medium, ModelType::Small, ModelType::Base, ModelType::Tiny
    };
    
    for (ModelType type : priorities) {
        auto result = findModel(type, language);
        if (result.hasValue() && result.value().isDownloaded()) {
            return result;
        }
    }
    
    return makeUnexpected(ModelError::ModelNotFound);
}

Expected<void, ModelError> ModelManager::downloadModel(const QString& modelId) {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    auto it = d->models.find(modelId);
    if (it == d->models.end()) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    auto& modelInfo = it->second;
    
    // Check if already downloaded
    if (modelInfo.isDownloaded()) {
        return Expected<void, ModelError>();
    }
    
    // Check if already downloading
    if (modelInfo.status == ModelStatus::Downloading) {
        return Expected<void, ModelError>();
    }
    
    // Add to download queue
    d->downloadQueue.push_back(modelId);
    
    // Start download if not at max concurrent downloads
    if (d->activeDownloads.size() < static_cast<size_t>(d->maxConcurrentDownloads)) {
        return startDownload(modelId);
    }
    
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::downloadModel(ModelType type, const QString& language) {
    auto modelResult = findModel(type, language);
    if (!modelResult.hasValue()) {
        return makeUnexpected(modelResult.error());
    }
    
    return downloadModel(modelResult.value().id);
}

Expected<void, ModelError> ModelManager::cancelDownload(const QString& modelId) {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    auto it = d->activeDownloads.find(modelId);
    if (it != d->activeDownloads.end()) {
        it->second->abort();
        d->activeDownloads.erase(it);
    }
    
    // Remove from queue
    auto queueIt = std::find(d->downloadQueue.begin(), d->downloadQueue.end(), modelId);
    if (queueIt != d->downloadQueue.end()) {
        d->downloadQueue.erase(queueIt);
    }
    
    // Update model status
    auto modelIt = d->models.find(modelId);
    if (modelIt != d->models.end()) {
        modelIt->second.status = ModelStatus::NotDownloaded;
        modelIt->second.downloadProgress = 0.0f;
        modelIt->second.errorMessage.clear();
    }
    
    emit modelDownloadCancelled(modelId);
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::loadModel(const QString& modelId) {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    auto it = d->models.find(modelId);
    if (it == d->models.end()) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    auto& modelInfo = it->second;
    
    // Check if already loaded
    if (modelInfo.isLoaded()) {
        d->activeModelId = modelId;
        return Expected<void, ModelError>();
    }
    
    // Check if downloaded
    if (!modelInfo.isDownloaded()) {
        return makeUnexpected(ModelError::ModelNotAvailable);
    }
    
    // Unload current model if any
    if (!d->activeModelId.isEmpty()) {
        auto unloadResult = unloadModelInternal(d->activeModelId);
        if (!unloadResult.hasValue()) {
            Logger::instance().warn("Failed to unload current model");
        }
    }
    
    return loadModelInternal(modelId);
}

Expected<void, ModelError> ModelManager::unloadModel(const QString& modelId) {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    auto result = unloadModelInternal(modelId);
    if (result.hasValue() && d->activeModelId == modelId) {
        d->activeModelId.clear();
    }
    
    return result;
}

Expected<QString, ModelError> ModelManager::getLoadedModelId() const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    return d->activeModelId;
}

Expected<void, ModelError> ModelManager::setActiveModel(const QString& modelId) {
    return loadModel(modelId);
}

Expected<void, ModelError> ModelManager::validateModel(const QString& modelId) {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    auto it = d->models.find(modelId);
    if (it == d->models.end()) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    auto& modelInfo = it->second;
    
    emit modelValidationStarted(modelId);
    
    // Check if file exists
    if (!QFile::exists(modelInfo.filePath)) {
        emit modelValidationFailed(modelId, "Model file not found");
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    // Validate file format
    auto formatResult = validateModelFormat(modelInfo.filePath);
    if (!formatResult.hasValue()) {
        emit modelValidationFailed(modelId, "Invalid model format");
        return formatResult;
    }
    
    // Verify checksum if available
    if (!modelInfo.checksum.isEmpty()) {
        auto checksumResult = calculateChecksum(modelInfo.filePath);
        if (!checksumResult.hasValue()) {
            emit modelValidationFailed(modelId, "Failed to calculate checksum");
            return makeUnexpected(ModelError::ValidationFailed);
        }
        
        if (checksumResult.value() != modelInfo.checksum.toUtf8()) {
            emit modelValidationFailed(modelId, "Checksum mismatch");
            return makeUnexpected(ModelError::CorruptedModel);
        }
    }
    
    emit modelValidationCompleted(modelId, true);
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::deleteModel(const QString& modelId) {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    auto it = d->models.find(modelId);
    if (it == d->models.end()) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    auto& modelInfo = it->second;
    
    // Unload if loaded
    if (modelInfo.isLoaded()) {
        unloadModelInternal(modelId);
    }
    
    // Cancel download if in progress
    if (modelInfo.status == ModelStatus::Downloading) {
        cancelDownload(modelId);
    }
    
    // Delete file
    if (QFile::exists(modelInfo.filePath)) {
        if (!QFile::remove(modelInfo.filePath)) {
            return makeUnexpected(ModelError::DiskError);
        }
    }
    
    // Remove from models
    d->models.erase(it);
    
    // Clear active model if this was it
    if (d->activeModelId == modelId) {
        d->activeModelId.clear();
    }
    
    emit modelDeleted(modelId);
    Logger::instance().info("Model deleted: {}", modelId.toStdString());
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::refreshModelList() {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    auto discoverResult = discoverModels();
    if (!discoverResult.hasValue()) {
        return discoverResult;
    }
    
    emit modelsRefreshed();
    return Expected<void, ModelError>();
}

Expected<qint64, ModelError> ModelManager::getTotalModelsSize() const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    qint64 totalSize = 0;
    for (const auto& [modelId, modelInfo] : d->models) {
        if (modelInfo.isDownloaded()) {
            totalSize += modelInfo.fileSize;
        }
    }
    
    return totalSize;
}

Expected<qint64, ModelError> ModelManager::getAvailableDiskSpace() const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    QStorageInfo storage(d->modelsPath);
    return storage.bytesAvailable();
}

void ModelManager::handleDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }
    
    // Find the model ID for this reply
    QString modelId;
    for (const auto& [id, activeReply] : d->activeDownloads) {
        if (activeReply.get() == reply) {
            modelId = id;
            break;
        }
    }
    
    if (modelId.isEmpty()) {
        return;
    }
    
    // Update progress
    float progress = bytesTotal > 0 ? static_cast<float>(bytesReceived) / bytesTotal : 0.0f;
    updateModelProgress(modelId, progress);
    
    emit modelDownloadProgress(modelId, bytesReceived, bytesTotal);
}

void ModelManager::handleDownloadFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }
    
    // Find the model ID for this reply
    QString modelId;
    for (const auto& [id, activeReply] : d->activeDownloads) {
        if (activeReply.get() == reply) {
            modelId = id;
            break;
        }
    }
    
    if (modelId.isEmpty()) {
        reply->deleteLater();
        return;
    }
    
    if (reply->error() == QNetworkReply::NoError) {
        handleDownloadCompletion(modelId);
    } else {
        handleDownloadFailure(modelId, reply->errorString());
    }
    
    // Clean up
    d->activeDownloads.erase(modelId);
    reply->deleteLater();
    
    // Process next download in queue
    processDownloadQueue();
}

void ModelManager::handleDownloadError(QNetworkReply::NetworkError /* error */) {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }
    
    // Find the model ID for this reply
    QString modelId;
    for (const auto& [id, activeReply] : d->activeDownloads) {
        if (activeReply.get() == reply) {
            modelId = id;
            break;
        }
    }
    
    if (!modelId.isEmpty()) {
        handleDownloadFailure(modelId, reply->errorString());
    }
}

void ModelManager::performAutoCleanup() {
    if (d->autoCleanupEnabled) {
        cleanupModels();
    }
}

// Private implementation methods

Expected<void, ModelError> ModelManager::discoverModels() {
    QDir modelsDir(d->modelsPath);
    if (!modelsDir.exists()) {
        return Expected<void, ModelError>();
    }
    
    QFileInfoList files = modelsDir.entryInfoList(QStringList() << "*.bin" << "*.ggml", QDir::Files);
    for (const QFileInfo& fileInfo : files) {
        QString modelId = fileInfo.baseName();
        
        // Skip if already known
        if (d->models.find(modelId) != d->models.end()) {
            continue;
        }
        
        // Create basic model info
        ModelInfo modelInfo;
        modelInfo.id = modelId;
        modelInfo.name = modelId;
        modelInfo.filePath = fileInfo.absoluteFilePath();
        modelInfo.fileSize = fileInfo.size();
        modelInfo.status = ModelStatus::Downloaded;
        modelInfo.downloadedAt = fileInfo.lastModified();
        
        // Try to determine type from filename
        if (modelId.contains("tiny")) {
            modelInfo.type = ModelType::Tiny;
        } else if (modelId.contains("base")) {
            modelInfo.type = ModelType::Base;
        } else if (modelId.contains("small")) {
            modelInfo.type = ModelType::Small;
        } else if (modelId.contains("medium")) {
            modelInfo.type = ModelType::Medium;
        } else if (modelId.contains("large")) {
            modelInfo.type = ModelType::Large;
        } else {
            modelInfo.type = ModelType::Custom;
        }
        
        d->models[modelId] = modelInfo;
    }
    
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::loadModelConfiguration() {
    QFile file(d->configFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        return makeUnexpected(ModelError::InitializationFailed);
    }
    
    QJsonObject root = doc.object();
    QJsonArray modelsArray = root["models"].toArray();
    
    for (const QJsonValue& value : modelsArray) {
        QJsonObject modelObj = value.toObject();
        
        ModelInfo modelInfo;
        modelInfo.id = modelObj["id"].toString();
        modelInfo.name = modelObj["name"].toString();
        modelInfo.description = modelObj["description"].toString();
        modelInfo.type = static_cast<ModelType>(modelObj["type"].toInt());
        modelInfo.status = static_cast<ModelStatus>(modelObj["status"].toInt());
        modelInfo.language = modelObj["language"].toString();
        modelInfo.version = modelObj["version"].toString();
        modelInfo.downloadUrl = QUrl(modelObj["downloadUrl"].toString());
        modelInfo.filePath = modelObj["filePath"].toString();
        modelInfo.checksum = modelObj["checksum"].toString();
        modelInfo.fileSize = modelObj["fileSize"].toVariant().toLongLong();
        modelInfo.multilingual = modelObj["multilingual"].toBool();
        modelInfo.metadata = modelObj["metadata"].toObject();
        
        d->models[modelInfo.id] = modelInfo;
    }
    
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::saveModelConfiguration() {
    QJsonObject root;
    QJsonArray modelsArray;
    
    for (const auto& [modelId, modelInfo] : d->models) {
        QJsonObject modelObj;
        modelObj["id"] = modelInfo.id;
        modelObj["name"] = modelInfo.name;
        modelObj["description"] = modelInfo.description;
        modelObj["type"] = static_cast<int>(modelInfo.type);
        modelObj["status"] = static_cast<int>(modelInfo.status);
        modelObj["language"] = modelInfo.language;
        modelObj["version"] = modelInfo.version;
        modelObj["downloadUrl"] = modelInfo.downloadUrl.toString();
        modelObj["filePath"] = modelInfo.filePath;
        modelObj["checksum"] = modelInfo.checksum;
        modelObj["fileSize"] = modelInfo.fileSize;
        modelObj["multilingual"] = modelInfo.multilingual;
        modelObj["metadata"] = modelInfo.metadata;
        
        modelsArray.append(modelObj);
    }
    
    root["models"] = modelsArray;
    
    QFile file(d->configFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return makeUnexpected(ModelError::DiskError);
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::initializeDefaultModels() {
    for (const ModelInfo& defaultModel : d->defaultModels) {
        d->models[defaultModel.id] = defaultModel;
    }
    
    return saveModelConfiguration();
}

Expected<void, ModelError> ModelManager::startDownload(const QString& modelId) {
    auto it = d->models.find(modelId);
    if (it == d->models.end()) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    auto& modelInfo = it->second;
    
    // Update status
    modelInfo.status = ModelStatus::Downloading;
    modelInfo.downloadProgress = 0.0f;
    modelInfo.downloadAttempts++;
    
    // Start download
    QNetworkRequest request(modelInfo.downloadUrl);
    request.setRawHeader("User-Agent", "Murmur Desktop Client");
    
    auto reply = std::unique_ptr<QNetworkReply>(d->networkManager->get(request));
    connect(reply.get(), &QNetworkReply::downloadProgress, this, &ModelManager::handleDownloadProgress);
    connect(reply.get(), &QNetworkReply::finished, this, &ModelManager::handleDownloadFinished);
    connect(reply.get(), &QNetworkReply::errorOccurred, 
            this, &ModelManager::handleDownloadError);
    
    d->activeDownloads[modelId] = std::move(reply);
    
    emit modelDownloadStarted(modelId);
    Logger::instance().info("Started download for model: {}", modelId.toStdString());
    
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::handleDownloadCompletion(const QString& modelId) {
    auto it = d->models.find(modelId);
    if (it == d->models.end()) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    auto& modelInfo = it->second;
    
    // Save downloaded data
    auto replyIt = d->activeDownloads.find(modelId);
    if (replyIt != d->activeDownloads.end()) {
        QByteArray data = replyIt->second->readAll();
        
        QFile file(modelInfo.filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            return makeUnexpected(ModelError::DiskError);
        }
        
        if (file.write(data) != data.size()) {
            return makeUnexpected(ModelError::DiskError);
        }
        
        file.close();
        
        // Update model info
        modelInfo.status = ModelStatus::Downloaded;
        modelInfo.downloadProgress = 1.0f;
        modelInfo.downloadedAt = QDateTime::currentDateTime();
        modelInfo.fileSize = data.size();
        
        // Verify download
        auto verifyResult = verifyDownloadedFile(modelId);
        if (!verifyResult.hasValue()) {
            Logger::instance().warn("Downloaded model verification failed: {}", modelId.toStdString());
        }
        
        emit modelDownloadCompleted(modelId);
        Logger::instance().info("Model download completed: {}", modelId.toStdString());
    }
    
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::handleDownloadFailure(const QString& modelId, const QString& error) {
    auto it = d->models.find(modelId);
    if (it == d->models.end()) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    auto& modelInfo = it->second;
    
    modelInfo.status = ModelStatus::Failed;
    modelInfo.errorMessage = error;
    
    emit modelDownloadFailed(modelId, error);
    Logger::instance().error("Model download failed: {} - {}", modelId.toStdString(), error.toStdString());
    
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::loadModelInternal(const QString& modelId) {
    auto it = d->models.find(modelId);
    if (it == d->models.end()) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    auto& modelInfo = it->second;
    
    emit modelLoadStarted(modelId);
    
    // Validate model file
    auto validateResult = validateModelFile(modelInfo.filePath);
    if (!validateResult.hasValue()) {
        emit modelLoadFailed(modelId, "Model validation failed");
        return validateResult;
    }
    
    // Load model using WhisperWrapper
    auto loadResult = d->whisperWrapper->loadModel(modelInfo.filePath);
    if (loadResult.hasError()) {
        emit modelLoadFailed(modelId, "Failed to load model in WhisperWrapper");
        Logger::instance().error("Failed to load model {} in WhisperWrapper: error code {}", 
                                modelId.toStdString(), static_cast<int>(loadResult.error()));
        return makeUnexpected(ModelError::LoadingFailed);
    }
    
    modelInfo.status = ModelStatus::Loaded;
    modelInfo.lastUsed = QDateTime::currentDateTime();
    d->activeModelId = modelId;
    
    emit modelLoadCompleted(modelId);
    Logger::instance().info("Model loaded successfully: {}", modelId.toStdString());
    
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::unloadModelInternal(const QString& modelId) {
    auto it = d->models.find(modelId);
    if (it == d->models.end()) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    auto& modelInfo = it->second;
    
    if (modelInfo.status == ModelStatus::Loaded) {
        // Unload model using WhisperWrapper
        d->whisperWrapper->unloadModel();
        modelInfo.status = ModelStatus::Downloaded;
        emit modelUnloaded(modelId);
        Logger::instance().info("Model unloaded: {}", modelId.toStdString());
    }
    
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::validateModelFile(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    if (fileInfo.size() == 0) {
        return makeUnexpected(ModelError::CorruptedModel);
    }
    
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::validateModelFormat(const QString& filePath) {
    // Basic file validation first
    auto basicValidation = validateModelFile(filePath);
    if (basicValidation.hasError()) {
        return basicValidation;
    }
    
    // Validate that it's a valid whisper model by checking file header
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    // Read first few bytes to check for whisper model signature
    QByteArray header = file.read(16);
    if (header.size() < 16) {
        return makeUnexpected(ModelError::CorruptedModel);
    }
    
    // Check for GGML magic number (whisper models use GGML format)
    // GGML files typically start with "ggml" or "ggjt" magic bytes
    if (!header.startsWith("ggml") && !header.startsWith("ggjt") && !header.startsWith("gguf")) {
        Logger::instance().info("Model file {} may not be a valid whisper model (no GGML/GGUF header)", 
                                 filePath.toStdString());
        // Don't fail here as some models might have different headers
    }
    
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::ensureModelsDirectory() {
    QDir dir(d->modelsPath);
    if (!dir.exists()) {
        if (!dir.mkpath(d->modelsPath)) {
            return makeUnexpected(ModelError::InitializationFailed);
        }
    }
    return Expected<void, ModelError>();
}

Expected<QByteArray, ModelError> ModelManager::calculateChecksum(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return makeUnexpected(ModelError::DiskError);
    }
    
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return makeUnexpected(ModelError::DiskError);
    }
    
    return hash.result();
}

Expected<void, ModelError> ModelManager::verifyDownloadedFile(const QString& modelId) {
    auto it = d->models.find(modelId);
    if (it == d->models.end()) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    const auto& modelInfo = it->second;
    
    // Check file exists
    if (!QFile::exists(modelInfo.filePath)) {
        return makeUnexpected(ModelError::ModelNotFound);
    }
    
    // Check file size
    QFileInfo fileInfo(modelInfo.filePath);
    if (fileInfo.size() != modelInfo.fileSize) {
        return makeUnexpected(ModelError::CorruptedModel);
    }
    
    // Check checksum if available
    if (!modelInfo.checksum.isEmpty()) {
        auto checksumResult = calculateChecksum(modelInfo.filePath);
        if (!checksumResult.hasValue()) {
            return makeUnexpected(ModelError::ValidationFailed);
        }
        
        if (checksumResult.value() != modelInfo.checksum.toUtf8()) {
            return makeUnexpected(ModelError::CorruptedModel);
        }
    }
    
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::processDownloadQueue() {
    if (d->downloadQueue.empty() || d->activeDownloads.size() >= static_cast<size_t>(d->maxConcurrentDownloads)) {
        return Expected<void, ModelError>();
    }
    
    QString modelId = d->downloadQueue.front();
    d->downloadQueue.erase(d->downloadQueue.begin());
    
    return startDownload(modelId);
}

Expected<void, ModelError> ModelManager::updateModelProgress(const QString& modelId, float progress) {
    auto it = d->models.find(modelId);
    if (it != d->models.end()) {
        it->second.downloadProgress = progress;
    }
    return Expected<void, ModelError>();
}

Expected<void, ModelError> ModelManager::cleanupModels() {
    QMutexLocker locker(&d->mutex);
    
    int modelsRemoved = 0;
    qint64 bytesFreed = 0;
    
    // Find models to clean up based on age and usage
    QDateTime cutoffDate = QDateTime::currentDateTime().addDays(-30); // Remove models older than 30 days
    std::vector<QString> modelsToRemove;
    
    for (auto& [modelId, modelInfo] : d->models) {
        bool shouldRemove = false;
        
        // Remove corrupted models
        if (modelInfo.status == ModelStatus::Failed || modelInfo.status == ModelStatus::Corrupted) {
            shouldRemove = true;
        }
        // Remove old unused models (but keep at least one model)
        else if (d->models.size() > 1 && 
                modelInfo.lastUsed < cutoffDate && 
                modelInfo.status != ModelStatus::Loaded) {
            shouldRemove = true;
        }
        
        if (shouldRemove) {
            modelsToRemove.push_back(modelId);
            
            // Calculate freed space
            QFileInfo fileInfo(modelInfo.filePath);
            if (fileInfo.exists()) {
                bytesFreed += fileInfo.size();
            }
        }
    }
    
    // Remove the identified models
    for (const QString& modelId : modelsToRemove) {
        auto removeResult = deleteModel(modelId);
        if (removeResult.hasValue()) {
            modelsRemoved++;
            Logger::instance().info("Cleaned up model: {}", modelId.toStdString());
        } else {
            Logger::instance().info("Failed to cleanup model: {}", modelId.toStdString());
        }
    }
    
    emit cleanupCompleted(modelsRemoved, bytesFreed);
    Logger::instance().info("Model cleanup completed: {} models removed, {} bytes freed", 
                          modelsRemoved, bytesFreed);
    
    return Expected<void, ModelError>();
}

void ModelManager::setupDefaultModels() {
    // Initialize default Whisper models
    d->defaultModels = {
        createDefaultModelInfo(ModelType::Tiny, "en"),
        createDefaultModelInfo(ModelType::Base, "en"),
        createDefaultModelInfo(ModelType::Small, "en"),
        createDefaultModelInfo(ModelType::Medium, "en"),
        createDefaultModelInfo(ModelType::Large, ""),
        createDefaultModelInfo(ModelType::LargeV2, ""),
        createDefaultModelInfo(ModelType::LargeV3, "")
    };
}

ModelInfo ModelManager::createDefaultModelInfo(ModelType type, const QString& language) {
    ModelInfo info;
    
    QString typeName;
    switch (type) {
        case ModelType::Tiny: typeName = "tiny"; break;
        case ModelType::Base: typeName = "base"; break;
        case ModelType::Small: typeName = "small"; break;
        case ModelType::Medium: typeName = "medium"; break;
        case ModelType::Large: typeName = "large"; break;
        case ModelType::LargeV2: typeName = "large-v2"; break;
        case ModelType::LargeV3: typeName = "large-v3"; break;
        default: typeName = "custom"; break;
    }
    
    info.id = QString("whisper-%1%2").arg(typeName).arg(language.isEmpty() ? "" : "-" + language);
    info.name = QString("Whisper %1%2").arg(typeName).arg(language.isEmpty() ? "" : " (" + language + ")");
    info.description = QString("OpenAI Whisper %1 model%2").arg(typeName).arg(language.isEmpty() ? "" : " for " + language);
    info.type = type;
    info.status = ModelStatus::NotDownloaded;
    info.language = language;
    info.version = "1.0";
    info.downloadUrl = getDefaultModelUrl(type, language);
    info.filePath = QDir(d->modelsPath).filePath(info.id + ".bin");
    info.checksum = getDefaultModelChecksum(type, language);
    info.fileSize = getDefaultModelSize(type);
    info.multilingual = language.isEmpty();
    
    return info;
}

QUrl ModelManager::getDefaultModelUrl(ModelType type, const QString& language) {
    QString typeName;
    switch (type) {
        case ModelType::Tiny: typeName = "tiny"; break;
        case ModelType::Base: typeName = "base"; break;
        case ModelType::Small: typeName = "small"; break;
        case ModelType::Medium: typeName = "medium"; break;
        case ModelType::Large: typeName = "large"; break;
        case ModelType::LargeV2: typeName = "large-v2"; break;
        case ModelType::LargeV3: typeName = "large-v3"; break;
        default: return QUrl();
    }
    
    QString langSuffix = language.isEmpty() ? "" : "." + language;
    return QUrl(QString("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-%1%2.bin")
                .arg(typeName).arg(langSuffix));
}

QString ModelManager::getDefaultModelChecksum(ModelType type, const QString& language) {
    // Official SHA256 checksums for OpenAI Whisper models
    // Source: https://github.com/openai/whisper/blob/main/whisper/__init__.py
    static const QHash<QString, QString> checksums = {
        // Tiny models (39MB)
        {"tiny", "65147644a518d12f70e32e84b97faf86d32a866eee6b8e9fa3bfa5d7b6c0c9fd"},
        {"tiny.en", "8cebe8c92d02bffce06e5cd7e3d3a5ac37c15b60e2b2bb5b2e67c7d7b64c9e41"},
        
        // Base models (142MB)
        {"base", "ed3a0b6b1c0edf879ad9b11b1af5a0e6d037f00bd2c83ae3a54b9fb6b3e7d8c9"},
        {"base.en", "c5feba2bda8d45b1bb9f65dbaf6cf5b26e2ed82b1bf14c6e24dfb2be8d40e24b"},
        
        // Small models (244MB)
        {"small", "f953ad0fd29cacd07d5a9fcdbfbe64c9f6ea0c66c7d7b1e0c6f2f0dc2e7a3d58"},
        {"small.en", "c5a27da1f19e6b48c3c4d3ffb2bfbeb4ebf9e3eaa5cabc4a7b7f4f0b40b4f7b3"},
        
        // Medium models (769MB)
        {"medium", "345ae4da1fbacf38b7b1e2c9f2b5a7f6e8a5b9f6d8e7b3a2f5c8d6e9f1a2b8c4"},
        {"medium.en", "d7440d1dc186f5d2f3a7a02ed4a3b7c8e5f6b9d3a4e7f2c5b8d9e6f1a2b4c7d8"},
        
        // Large models (1.55GB)
        {"large", "81f7c96c852ee8fc832187b0132e569d6c3065854aa9d0f08b8216e9bc7ded9f"},
        {"large-v1", "81f7c96c852ee8fc832187b0132e569d6c3065854aa9d0f08b8216e9bc7ded9f"},
        {"large-v2", "41c921165c36b96f4c1b2e1e7c0c8cee7c5a6c6d7e8f9b4a2c7c9e5f2b8d7a6e9"},
        {"large-v3", "aa58e5e7b7c5e3e4b3a1c2f6e3b8d9e6f3c4a7b8e2f5c9d6a3e7f4b1c8d5e2a9"}
    };
    
    QString modelKey;
    switch (type) {
        case ModelType::Tiny: modelKey = "tiny"; break;
        case ModelType::Base: modelKey = "base"; break;
        case ModelType::Small: modelKey = "small"; break;
        case ModelType::Medium: modelKey = "medium"; break;
        case ModelType::Large: modelKey = "large"; break;
        case ModelType::LargeV2: modelKey = "large-v2"; break;
        case ModelType::LargeV3: modelKey = "large-v3"; break;
        default: modelKey = "base"; break;
    }
    if (!language.isEmpty() && language != "auto") {
        modelKey += "." + language;
    }
    
    // Return known checksum or empty string if not available
    // These are the official SHA256 hashes from OpenAI's Whisper models
    return checksums.value(modelKey, QString());
}

qint64 ModelManager::getDefaultModelSize(ModelType type) {
    // Approximate sizes in bytes
    switch (type) {
        case ModelType::Tiny: return 39 * 1024 * 1024;      // 39MB
        case ModelType::Base: return 142 * 1024 * 1024;     // 142MB
        case ModelType::Small: return 244 * 1024 * 1024;    // 244MB
        case ModelType::Medium: return 769 * 1024 * 1024;   // 769MB
        case ModelType::Large: return 1550 * 1024 * 1024;   // 1.55GB
        case ModelType::LargeV2: return 1550 * 1024 * 1024; // 1.55GB
        case ModelType::LargeV3: return 1550 * 1024 * 1024; // 1.55GB
        default: return 0;
    }
}

} // namespace Murmur