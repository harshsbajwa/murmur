#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <memory>
#include <unordered_map>
#include <vector>

#include "core/common/Expected.hpp"
#include "core/common/Logger.hpp"

namespace Murmur {

enum class ModelError {
    InitializationFailed,
    ModelNotFound,
    ModelNotAvailable,
    DownloadFailed,
    LoadingFailed,
    ValidationFailed,
    InvalidConfiguration,
    NetworkError,
    DiskError,
    MemoryError,
    CorruptedModel,
    UnsupportedModel,
    PermissionDenied
};

enum class ModelType {
    Tiny,
    Base,
    Small,
    Medium,
    Large,
    LargeV2,
    LargeV3,
    Custom
};

enum class ModelStatus {
    NotDownloaded,
    Downloading,
    Downloaded,
    Loading,
    Loaded,
    Failed,
    Corrupted
};

struct ModelInfo {
    QString id;
    QString name;
    QString description;
    ModelType type;
    ModelStatus status;
    QString language;
    QString version;
    QUrl downloadUrl;
    QString filePath;
    QString checksum;
    qint64 fileSize;
    qint64 downloadedSize;
    float downloadProgress;
    QDateTime lastUsed;
    QDateTime downloadedAt;
    QJsonObject metadata;
    bool multilingual;
    int downloadAttempts;
    QString errorMessage;
    
    // Performance metrics
    float averageSpeed;    // tokens per second
    float memoryUsage;     // MB
    float accuracy;        // 0.0 to 1.0
    
    bool isValid() const {
        return !id.isEmpty() && !name.isEmpty() && !filePath.isEmpty();
    }
    
    bool isDownloaded() const {
        return status == ModelStatus::Downloaded || status == ModelStatus::Loaded;
    }
    
    bool isLoaded() const {
        return status == ModelStatus::Loaded;
    }
    
    QString getDisplayName() const {
        return name.isEmpty() ? id : name;
    }
};

class ModelManager : public QObject {
    Q_OBJECT

public:
    explicit ModelManager(QObject* parent = nullptr);
    ~ModelManager() override;

    // Initialization
    Expected<void, ModelError> initialize(const QString& modelsPath);
    Expected<void, ModelError> shutdown();
    bool isInitialized() const;

    // Model discovery and management
    Expected<std::vector<ModelInfo>, ModelError> getAvailableModels() const;
    Expected<std::vector<ModelInfo>, ModelError> getDownloadedModels() const;
    Expected<std::vector<ModelInfo>, ModelError> getLoadedModels() const;
    Expected<ModelInfo, ModelError> getModelInfo(const QString& modelId) const;
    Expected<ModelInfo, ModelError> findModel(ModelType type, const QString& language = QString()) const;
    Expected<ModelInfo, ModelError> findBestModel(const QString& language = QString()) const;

    // Model downloading
    Expected<void, ModelError> downloadModel(const QString& modelId);
    Expected<void, ModelError> downloadModel(ModelType type, const QString& language = QString());
    Expected<void, ModelError> cancelDownload(const QString& modelId);
    Expected<void, ModelError> pauseDownload(const QString& modelId);
    Expected<void, ModelError> resumeDownload(const QString& modelId);
    Expected<void, ModelError> retryDownload(const QString& modelId);

    // Model loading and unloading
    Expected<void, ModelError> loadModel(const QString& modelId);
    Expected<void, ModelError> unloadModel(const QString& modelId);
    Expected<void, ModelError> preloadModel(const QString& modelId);
    Expected<QString, ModelError> getLoadedModelId() const;
    Expected<void, ModelError> setActiveModel(const QString& modelId);

    // Model validation and verification
    Expected<void, ModelError> validateModel(const QString& modelId);
    Expected<void, ModelError> verifyModelIntegrity(const QString& modelId);
    Expected<void, ModelError> repairModel(const QString& modelId);

    // Model management
    Expected<void, ModelError> deleteModel(const QString& modelId);
    Expected<void, ModelError> cleanupModels();
    Expected<void, ModelError> refreshModelList();
    Expected<void, ModelError> updateModelMetadata(const QString& modelId, const QJsonObject& metadata);

    // Configuration
    Expected<void, ModelError> setModelsPath(const QString& path);
    Expected<QString, ModelError> getModelsPath() const;
    Expected<void, ModelError> setDownloadTimeout(int timeoutMs);
    Expected<void, ModelError> setMaxConcurrentDownloads(int maxDownloads);
    Expected<void, ModelError> setMaxRetryAttempts(int maxAttempts);
    Expected<void, ModelError> setAutoCleanupEnabled(bool enabled);
    Expected<void, ModelError> setAutoCleanupInterval(int intervalMs);

    // Statistics and monitoring
    Expected<qint64, ModelError> getTotalModelsSize() const;
    Expected<qint64, ModelError> getAvailableDiskSpace() const;
    Expected<int, ModelError> getDownloadedModelsCount() const;
    Expected<int, ModelError> getLoadedModelsCount() const;
    Expected<float, ModelError> getOverallDownloadProgress() const;

    // Custom models
    Expected<void, ModelError> addCustomModel(const ModelInfo& modelInfo);
    Expected<void, ModelError> removeCustomModel(const QString& modelId);
    Expected<void, ModelError> importModel(const QString& filePath, const QString& modelId = QString());
    Expected<void, ModelError> exportModel(const QString& modelId, const QString& filePath);

    // Backup and restore
    Expected<void, ModelError> backupModels(const QString& backupPath);
    Expected<void, ModelError> restoreModels(const QString& backupPath);

signals:
    void modelDownloadStarted(const QString& modelId);
    void modelDownloadProgress(const QString& modelId, qint64 bytesReceived, qint64 bytesTotal);
    void modelDownloadCompleted(const QString& modelId);
    void modelDownloadFailed(const QString& modelId, const QString& error);
    void modelDownloadCancelled(const QString& modelId);
    void modelDownloadPaused(const QString& modelId);
    void modelDownloadResumed(const QString& modelId);
    
    void modelLoadStarted(const QString& modelId);
    void modelLoadCompleted(const QString& modelId);
    void modelLoadFailed(const QString& modelId, const QString& error);
    void modelUnloaded(const QString& modelId);
    
    void modelValidationStarted(const QString& modelId);
    void modelValidationCompleted(const QString& modelId, bool valid);
    void modelValidationFailed(const QString& modelId, const QString& error);
    
    void modelDeleted(const QString& modelId);
    void modelCorrupted(const QString& modelId);
    void modelRepaired(const QString& modelId);
    
    void modelsRefreshed();
    void cleanupCompleted(int modelsRemoved, qint64 bytesFreed);
    
    void diskSpaceWarning(qint64 availableBytes, qint64 requiredBytes);
    void memoryWarning(qint64 usedBytes, qint64 availableBytes);

private slots:
    void handleDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void handleDownloadFinished();
    void handleDownloadError(QNetworkReply::NetworkError error);
    void performAutoCleanup();

private:
    class ModelManagerPrivate;
    std::unique_ptr<ModelManagerPrivate> d;

    // Model discovery and initialization
    Expected<void, ModelError> discoverModels();
    Expected<void, ModelError> loadModelConfiguration();
    Expected<void, ModelError> saveModelConfiguration();
    Expected<void, ModelError> initializeDefaultModels();

    // Download management
    Expected<void, ModelError> startDownload(const QString& modelId);
    Expected<void, ModelError> processDownloadQueue();
    Expected<void, ModelError> handleDownloadCompletion(const QString& modelId);
    Expected<void, ModelError> handleDownloadFailure(const QString& modelId, const QString& error);

    // Model loading/unloading
    Expected<void, ModelError> loadModelInternal(const QString& modelId);
    Expected<void, ModelError> unloadModelInternal(const QString& modelId);
    Expected<void, ModelError> validateModelFile(const QString& filePath);

    // File operations
    Expected<void, ModelError> ensureModelsDirectory();
    Expected<QString, ModelError> generateModelFilePath(const QString& modelId);
    Expected<QByteArray, ModelError> calculateChecksum(const QString& filePath);
    Expected<void, ModelError> createModelInfoFile(const ModelInfo& modelInfo);
    Expected<ModelInfo, ModelError> loadModelInfoFile(const QString& filePath);

    // Network operations
    Expected<void, ModelError> downloadModelFile(const QString& modelId, const QUrl& url);
    Expected<void, ModelError> verifyDownloadedFile(const QString& modelId);
    Expected<QUrl, ModelError> resolveModelUrl(const QString& modelId);

    // Validation and verification
    Expected<void, ModelError> validateModelFormat(const QString& filePath);
    Expected<void, ModelError> validateModelMetadata(const ModelInfo& modelInfo);
    Expected<void, ModelError> checkModelCompatibility(const ModelInfo& modelInfo);

    // Cleanup and maintenance
    Expected<void, ModelError> cleanupCorruptedModels();
    Expected<void, ModelError> cleanupOldModels();
    Expected<void, ModelError> cleanupTempFiles();
    Expected<qint64, ModelError> calculateModelsSize();

    // Configuration helpers
    Expected<void, ModelError> updateModelStatus(const QString& modelId, ModelStatus status);
    Expected<void, ModelError> updateModelProgress(const QString& modelId, float progress);
    Expected<void, ModelError> updateModelError(const QString& modelId, const QString& error);
    Expected<void, ModelError> updateModelMetrics(const QString& modelId, float speed, float memory, float accuracy);

    // Default model configurations
    void setupDefaultModels();
    ModelInfo createDefaultModelInfo(ModelType type, const QString& language = QString());
    QUrl getDefaultModelUrl(ModelType type, const QString& language = QString());
    QString getDefaultModelChecksum(ModelType type, const QString& language = QString());
    qint64 getDefaultModelSize(ModelType type);
};

} // namespace Murmur