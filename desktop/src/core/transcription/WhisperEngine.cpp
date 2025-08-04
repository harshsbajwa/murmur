#include "WhisperEngine.hpp"
#include "WhisperWrapper.hpp"
#include "ModelDownloader.hpp"
#include "TranscriptionFormatter.hpp" // Added for format conversion
#include "../common/Logger.hpp"
#include "../security/InputValidator.hpp"

#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMutexLocker>
#include <QtConcurrent>
#include <QUuid>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QUrl>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QAudioSource>
#include <QRegularExpression>
#include <QTextStream>
#include <QFile>
#include <cmath>

// Platform-specific includes for memory monitoring
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
#include <sys/resource.h>
#endif

namespace Murmur {

// Static member initialization
const QStringList WhisperEngine::AVAILABLE_MODELS = {
    "tiny", "tiny.en", "base", "base.en", "small", "small.en",
    "medium", "medium.en", "large-v1", "large-v2", "large-v3",
    "tiny-q5_1"  // Add quantized model support
};

const QHash<QString, qint64> WhisperEngine::MODEL_SIZES = {
    {"tiny", 39 * 1024 * 1024},        // 39MB
    {"tiny.en", 39 * 1024 * 1024},     // 39MB
    {"base", 142 * 1024 * 1024},       // 142MB
    {"base.en", 142 * 1024 * 1024},    // 142MB
    {"small", 244 * 1024 * 1024},      // 244MB
    {"small.en", 244 * 1024 * 1024},   // 244MB
    {"medium", 769 * 1024 * 1024},     // 769MB
    {"medium.en", 769 * 1024 * 1024},  // 769MB
    {"large-v1", 1550 * 1024 * 1024},  // 1550MB
    {"large-v2", 1550 * 1024 * 1024},  // 1550MB
    {"large-v3", 1550 * 1024 * 1024},  // 1550MB
    {"tiny-q5_1", 31 * 1024 * 1024}    // 31MB (quantized version)
};

const QStringList WhisperEngine::SUPPORTED_LANGUAGES = {
    "auto", "en", "zh", "de", "es", "ru", "ko", "fr", "ja", "pt", "tr", "pl",
    "ca", "nl", "ar", "sv", "it", "id", "hi", "fi", "vi", "he", "uk", "el",
    "ms", "cs", "ro", "da", "hu", "ta", "no", "th", "ur", "hr", "bg", "lt",
    "la", "mi", "ml", "cy", "sk", "te", "fa", "lv", "bn", "sr", "az", "sl",
    "kn", "et", "mk", "br", "eu", "is", "hy", "ne", "mn", "bs", "kk", "sq",
    "sw", "gl", "mr", "pa", "si", "km", "sn", "yo", "so", "af", "oc", "ka",
    "be", "tg", "sd", "gu", "am", "yi", "lo", "uz", "fo", "ht", "ps", "tk",
    "nn", "mt", "sa", "lb", "my", "bo", "tl", "mg", "as", "tt", "haw", "ln",
    "ha", "ba", "jw", "su"
};

const QString WhisperEngine::WHISPER_CPP_REPO_URL = "https://huggingface.co/ggerganov/whisper.cpp";
const QString WhisperEngine::AUDIO_FORMAT = "wav";
const QString WhisperEngine::PROGRESS_PATTERN = R"(\[(\d+):(\d+)\.(\d+) --> (\d+):(\d+)\.(\d+)\])";
const QString WhisperEngine::SEGMENT_PATTERN = R"((\d+):(\d+)\.(\d+) --> (\d+):(\d+)\.(\d+))";
const QString WhisperEngine::TIMESTAMP_PATTERN = R"(\[(\d+\.\d+)s -> (\d+\.\d+)s\])";

WhisperEngine::WhisperEngine(QObject* parent)
    : QObject(parent)
    , whisperWrapper_(std::make_unique<WhisperWrapper>())
    , modelDownloader_(std::make_unique<ModelDownloader>(this)) {

    performanceStats_.lastReset = QDateTime::currentDateTime();

    modelsPath_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";
    QDir().mkpath(modelsPath_);

    // Connect signals for the downloader
    connect(modelDownloader_.get(), &ModelDownloader::downloadCompleted,
            this, [](const QString& downloadId, const QString& localPath) {
                Q_UNUSED(downloadId)
                Logger::instance().info("WhisperEngine: Model download completed: {}", localPath.toStdString());
            });
    
    connect(modelDownloader_.get(), &ModelDownloader::downloadFailed,
            this, [](const QString& downloadId, DownloadError error, const QString& errorMessage) {
                Q_UNUSED(downloadId)
                Logger::instance().error("WhisperEngine: Model download failed: {} ({})", 
                                          errorMessage.toStdString(), static_cast<int>(error));
            });
    
    connect(modelDownloader_.get(), &ModelDownloader::downloadProgress,
            this, [](const QString& downloadId, qint64 bytesReceived, qint64 bytesTotal, double speed) {
                Q_UNUSED(downloadId)
                double progress = bytesTotal > 0 ? (static_cast<double>(bytesReceived) / static_cast<double>(bytesTotal)) * 100.0 : 0.0;
                Logger::instance().info("WhisperEngine: Model download progress: {:.1f}% ({:.1f} KB/s)", 
                                         progress, speed / 1024.0);
            });

    Logger::instance().info("WhisperEngine: created with models path: {}", modelsPath_.toStdString());
}

WhisperEngine::~WhisperEngine() {
    shutdown();
}

Expected<bool, TranscriptionError> WhisperEngine::initialize(const QString& modelsPath) {
    if (!modelsPath.isEmpty()) {
        // Check if the provided path exists before trying to use it
        QDir dir(modelsPath);
        if (!dir.exists()) {
            Logger::instance().error("WhisperEngine: Models path does not exist: {}", modelsPath.toStdString());
            return makeUnexpected(TranscriptionError::ModelNotLoaded);
        }
        
        modelsPath_ = modelsPath;
        QDir().mkpath(modelsPath_);
    }

    auto initResult = initializeWhisperCpp();
    if (initResult.hasError()) {
        return initResult;
    }

    auto loadResult = loadModel("base");
    if (loadResult.hasError()) {
        Logger::instance().warn("WhisperEngine: Failed to load default model, will need manual model loading");
    }

    isInitialized_ = true;
    Logger::instance().info("WhisperEngine: initialized successfully");
    return true;
}

void WhisperEngine::shutdown() {
    cancelAllTranscriptions();

    QMutexLocker locker(&tasksMutex_);
    for (const auto& pair : realtimeSessions_) {
        cleanupRealtimeSession(pair.first);
    }
    realtimeSessions_.clear();

    unloadModel();
    isInitialized_ = false;

    Logger::instance().info("WhisperEngine: shutdown completed");
}

bool WhisperEngine::isInitialized() const {
    return isInitialized_;
}

bool WhisperEngine::isReady() const {
    // For test compatibility - return true if initialized and has a model loaded
    return isInitialized_ && !currentModel_.isEmpty() && whisperWrapper_;
}

Expected<bool, TranscriptionError> WhisperEngine::downloadModel(const QString& modelSize) {
    if (!AVAILABLE_MODELS.contains(modelSize)) {
        Logger::instance().error("WhisperEngine: Unsupported model size requested: {}", modelSize.toStdString());
        return makeUnexpected(TranscriptionError::ModelDownloadFailed);
    }

    QString modelPath = getModelPath(modelSize);
    if (QFileInfo::exists(modelPath)) {
        Logger::instance().info("WhisperEngine: Model already exists: {}", modelSize.toStdString());
        return true;
    }

    return downloadModelFromHub(modelSize);
}

Expected<bool, TranscriptionError> WhisperEngine::downloadModelFromHub(const QString& modelSize) {
    QString modelUrl = getModelUrl(modelSize);
    QString modelPath = getModelPath(modelSize);

    QDir modelsDir(modelsPath_);
    if (!modelsDir.exists()) {
        if (!modelsDir.mkpath(".")) {
            Logger::instance().error("WhisperEngine: Failed to create models directory: {}", modelsPath_.toStdString());
            return makeUnexpected(TranscriptionError::ModelDownloadFailed);
        }
    }

    Logger::instance().info("WhisperEngine: Starting model download: {}", modelUrl.toStdString());
    
    auto result = modelDownloader_->downloadFile(modelUrl, modelPath);
    
    if (result.hasError()) {
        Logger::instance().error("WhisperEngine: Model download failed with error code: {}", static_cast<int>(result.error()));
        return makeUnexpected(TranscriptionError::ModelDownloadFailed);
    }
    
    if (!QFileInfo::exists(modelPath)) {
        Logger::instance().error("WhisperEngine: Model file not found after download: {}", modelPath.toStdString());
        return makeUnexpected(TranscriptionError::ModelDownloadFailed);
    }
    
    Logger::instance().info("WhisperEngine: Model download successful: {}", modelPath.toStdString());
    return true;
}

Expected<bool, TranscriptionError> WhisperEngine::loadModel(const QString& modelSize) {
    if (!AVAILABLE_MODELS.contains(modelSize)) {
        Logger::instance().error("WhisperEngine: Unsupported model size: {}", modelSize.toStdString());
        return makeUnexpected(TranscriptionError::ModelNotLoaded);
    }

    QString modelPath = getModelPath(modelSize);
    if (!QFileInfo::exists(modelPath)) {
        Logger::instance().info("WhisperEngine: Model file not found, attempting to download: {}", modelPath.toStdString());
        
        // Attempt automatic download
        auto downloadResult = downloadModelFromHub(modelSize);
        if (downloadResult.hasError()) {
            Logger::instance().error("WhisperEngine: Failed to download model {}: {}", 
                                   modelSize.toStdString(), static_cast<int>(downloadResult.error()));
            return downloadResult;
        }
        
        // Verify the model was downloaded successfully
        if (!QFileInfo::exists(modelPath)) {
            Logger::instance().error("WhisperEngine: Model file still not found after download: {}", modelPath.toStdString());
            return makeUnexpected(TranscriptionError::ModelNotLoaded);
        }
        
        Logger::instance().info("WhisperEngine: Successfully downloaded model: {}", modelSize.toStdString());
    }

    auto verifyResult = verifyModelIntegrity(modelPath);
    if (verifyResult.hasError()) {
        return verifyResult;
    }

    auto loadResult = whisperWrapper_->loadModel(modelPath);
    if (loadResult.hasError()) {
        Logger::instance().error("WhisperEngine: Failed to load model in WhisperWrapper: {}", modelPath.toStdString());
        return makeUnexpected(convertWhisperError(loadResult.error()));
    }

    currentModel_ = modelSize;
    Logger::instance().info("WhisperEngine: Loaded model: {}", modelSize.toStdString());
    return true;
}

void WhisperEngine::unloadModel() {
    whisperWrapper_->unloadModel();
    currentModel_.clear();
}

QString WhisperEngine::getCurrentModel() const {
    return currentModel_;
}

QStringList WhisperEngine::getAvailableModels() const {
    return AVAILABLE_MODELS;
}

QStringList WhisperEngine::getSupportedLanguages() const {
    return SUPPORTED_LANGUAGES;
}

QFuture<Expected<TranscriptionResult, TranscriptionError>> WhisperEngine::transcribeAudio(
    const QString& audioFilePath,
    const TranscriptionSettings& settings) {

    return QtConcurrent::run([this, audioFilePath, settings]() -> Expected<TranscriptionResult, TranscriptionError> {
        // Validate input first, regardless of model state
        if (!InputValidator::isValidMediaFile(audioFilePath)) {
            return makeUnexpected(TranscriptionError::InvalidAudioFormat);
        }
        
        // Validate language setting
        if (!settings.language.isEmpty() && settings.language != "auto" && 
            !InputValidator::validateLanguageCode(settings.language)) {
            return makeUnexpected(TranscriptionError::UnsupportedLanguage);
        }

        // Mutex locker to serialize transcription tasks
        QMutexLocker locker(&whisperMutex_);

        // Then check if we have a model loaded
        if (!isInitialized_ || currentModel_.isEmpty()) {
            return makeUnexpected(TranscriptionError::ModelNotLoaded);
        }

        // Load audio into the format whisper.cpp needs
        auto audioDataResult = whisperWrapper_->loadAudioFile(audioFilePath);
        if (audioDataResult.hasError()) {
            return makeUnexpected(convertWhisperError(audioDataResult.error()));
        }

        // Create task for progress tracking
        QString taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto task = std::make_unique<TranscriptionTask>();
        task->taskId = taskId;
        task->audioFile = audioFilePath;
        task->startTime = QDateTime::currentMSecsSinceEpoch();
        task->isCancelled = false;
        
        // Get audio duration for progress calculation
        auto durationResult = getAudioDuration(audioFilePath);
        task->audioDuration = durationResult.hasValue() ? durationResult.value() : 0;
        
        {
            QMutexLocker tlocker(&tasksMutex_);
            activeTasks_[taskId] = std::move(task);
        }

        // Configure whisper.cpp parameters
        WhisperConfig config;
        config.language = settings.language;
        config.enableTimestamps = settings.enableTimestamps;
        config.enableTokenTimestamps = settings.enableWordConfidence;
        config.temperature = settings.temperature;
        config.beamSize = settings.beamSize;
        config.nThreads = QThread::idealThreadCount();

        // Emit initial progress
        updateTaskProgress(taskId, 0.0);

        // Asynchronously transcribe
        auto result = whisperWrapper_->transcribe(audioDataResult.value(), config);
        if (result.hasError()) {
            // Clean up task
            {
                QMutexLocker tlocker(&tasksMutex_);
                activeTasks_.erase(taskId);
            }
            Logger::instance().error("WhisperEngine: Transcription failed with error: {}", static_cast<int>(result.error()));
            return makeUnexpected(convertWhisperError(result.error()));
        }

        // Emit progress at 50% (processing completed, converting results)
        updateTaskProgress(taskId, 50.0);

        // Convert result to our engine's format
        TranscriptionResult finalResult = convertWhisperResult(result.value(), settings);
        
        // Emit completion progress
        updateTaskProgress(taskId, 100.0);

        // Update performance stats and clean up task
        qint64 audioDuration = 0;
        {
            QMutexLocker tlocker(&tasksMutex_);
            auto it = activeTasks_.find(taskId);
            if (it != activeTasks_.end()) {
                audioDuration = it->second->audioDuration;
                activeTasks_.erase(it);
            }
            
            performanceStats_.totalTranscriptions++;
            performanceStats_.totalProcessingTime += finalResult.processingTime;
            performanceStats_.totalAudioDuration += audioDuration;

            if (audioDuration > 0) {
                double rtf = static_cast<double>(finalResult.processingTime) / audioDuration;
                performanceStats_.averageRealTimeFactor =
                    (performanceStats_.averageRealTimeFactor * (performanceStats_.totalTranscriptions - 1) + rtf) / performanceStats_.totalTranscriptions;
            }
        }

        // Emit completion signal with final result
        emit transcriptionCompleted(taskId, finalResult);

        return finalResult;
    });
}

QFuture<Expected<TranscriptionResult, TranscriptionError>> WhisperEngine::transcribeFromVideo(
    const QString& videoFilePath,
    const TranscriptionSettings& settings) {

    // Create a promise to handle the async operation properly
    auto promise = std::make_shared<QPromise<Expected<TranscriptionResult, TranscriptionError>>>();
    auto future = promise->future();
    promise->start();

    // First, extract audio from video asynchronously
    QtConcurrent::run([this, videoFilePath, settings, promise]() {
        if (!InputValidator::isValidMediaFile(videoFilePath)) {
            promise->addResult(makeUnexpected(TranscriptionError::InvalidAudioFormat));
            promise->finish();
            return;
        }

        // Create temporary directory (this can be done synchronously as it's fast)
        auto tempDirResult = createTempDirectory();
        if (tempDirResult.hasError()) {
            promise->addResult(makeUnexpected(tempDirResult.error()));
            promise->finish();
            return;
        }

        QString tempDir = tempDirResult.value();
        QString audioPath = tempDir + "/extracted_audio.wav";

        // Extract audio from video
        auto extractResult = extractAudioFromVideo(videoFilePath, audioPath);
        if (extractResult.hasError()) {
            cleanupTempDirectory(tempDir);
            promise->addResult(makeUnexpected(extractResult.error()));
            promise->finish();
            return;
        }

        // Now transcribe the extracted audio asynchronously without blocking
        auto transcribeTask = transcribeAudio(audioPath, settings);
        
        // Create a watcher to handle completion
        auto watcher = new QFutureWatcher<Expected<TranscriptionResult, TranscriptionError>>();
        QObject::connect(watcher, &QFutureWatcher<Expected<TranscriptionResult, TranscriptionError>>::finished,
                        [promise, tempDir, watcher, this]() {
            auto result = watcher->result();
            cleanupTempDirectory(tempDir);  // Clean up temp directory
            promise->addResult(result);
            promise->finish();
            watcher->deleteLater();
        });
        
        watcher->setFuture(transcribeTask);
    });

    return future;
}

void WhisperEngine::cancelTranscription(const QString& taskId) {
    QMutexLocker locker(&tasksMutex_);

    auto it = activeTasks_.find(taskId);
    if (it != activeTasks_.end()) {
        it->second->isCancelled = true;
        if (it->second->process) {
            it->second->process->kill();
        }
    }
}

void WhisperEngine::cancelAllTranscriptions() {
    whisperWrapper_->requestCancel();
    // Also cancel any command-line processes if that path is used
    QMutexLocker locker(&tasksMutex_);
    for (const auto& pair : activeTasks_) {
        pair.second->isCancelled = true;
        if (pair.second->process) {
            pair.second->process->kill();
        }
    }
}

QStringList WhisperEngine::getActiveTranscriptions() const {
    QMutexLocker locker(&tasksMutex_);
    QStringList keys;
    keys.reserve(activeTasks_.size());
    for (const auto& pair : activeTasks_) {
        keys.append(pair.first);
    }
    return keys;
}

// Private implementation methods
Expected<bool, TranscriptionError> WhisperEngine::initializeWhisperCpp() {
    auto initResult = whisperWrapper_->initialize();
    if (initResult.hasError()) {
        Logger::instance().error("WhisperEngine: Failed to initialize whisper.cpp library");
        return makeUnexpected(convertWhisperError(initResult.error()));
    }

    Logger::instance().info("WhisperEngine: whisper.cpp library initialized successfully, version: {}",
                 WhisperWrapper::getLibraryVersion().toStdString());

    if (WhisperWrapper::hasGpuSupport() && gpuEnabled_) {
        Logger::instance().info("WhisperEngine: GPU support detected and enabled");
    } else {
        Logger::instance().info("WhisperEngine: Using CPU-only transcription");
    }

    return true;
}

QString WhisperEngine::getWhisperExecutablePath() const {
    // Try common locations for whisper.cpp executable
    QStringList candidates = {
        "whisper",  // System PATH
        QCoreApplication::applicationDirPath() + "/whisper",
        QCoreApplication::applicationDirPath() + "/bin/whisper",
        "/usr/local/bin/whisper",
        "/opt/homebrew/bin/whisper"
    };

    for (const QString& candidate : candidates) {
        QProcess testProcess;
        testProcess.start(candidate, {"--version"});
        if (testProcess.waitForFinished(3000) && testProcess.exitCode() == 0) {
            return candidate;
        }
    }

    return QString();
}

QStringList WhisperEngine::buildTranscriptionCommand(const TranscriptionTask& task) {
    QStringList args;

    // Input file
    args << "-f" << task.audioFile;

    // Model
    args << "-m" << getModelPath(currentModel_);

    // Language
    if (task.settings.language != "auto") {
        args << "-l" << task.settings.language;
    }

    // Output format
    if (task.settings.outputFormat == "json") {
        args << "--output-json";
    } else if (task.settings.outputFormat == "srt") {
        args << "--output-srt";
    } else if (task.settings.outputFormat == "vtt") {
        args << "--output-vtt";
    }

    // Timestamps
    if (task.settings.enableTimestamps) {
        args << "--output-words";
    }

    // Processing options
    args << "--threads" << QString::number(QThread::idealThreadCount());

    if (gpuEnabled_) {
        args << "--gpu";
    }

    // Beam search
    args << "--beam-size" << QString::number(task.settings.beamSize);

    // Temperature
    if (task.settings.temperature > 0.0) {
        args << "--temperature" << QString::number(task.settings.temperature);
    }

    // Output to stdout for parsing
    args << "--output-file" << "-";

    return args;
}

Expected<QString, TranscriptionError> WhisperEngine::preprocessAudio(
    const QString& inputFile,
    const QString& outputFile) {

    QProcess ffmpegProcess;
    QStringList args;

    // Input file
    args << "-i" << inputFile;

    // Audio processing for Whisper (16kHz, mono, WAV)
    args << "-ar" << QString::number(SAMPLE_RATE);
    args << "-ac" << QString::number(CHANNELS);
    args << "-c:a" << "pcm_s16le";

    // Output file
    args << "-y" << outputFile;

    ffmpegProcess.start("ffmpeg", args);

    if (!ffmpegProcess.waitForFinished(60000)) {  // 60 second timeout
        ffmpegProcess.kill();
        return makeUnexpected(TranscriptionError::AudioProcessingFailed);
    }

    if (ffmpegProcess.exitCode() != 0) {
        Logger::instance().error("WhisperEngine: Audio preprocessing failed: {}", ffmpegProcess.readAllStandardError().toStdString());
        return makeUnexpected(TranscriptionError::AudioProcessingFailed);
    }

    return outputFile;
}

Expected<qint64, TranscriptionError> WhisperEngine::getAudioDuration(const QString& audioFile) {
    QProcess ffprobeProcess;
    QStringList args;

    args << "-v" << "quiet";
    args << "-print_format" << "json";
    args << "-show_format";
    args << audioFile;

    ffprobeProcess.start("ffprobe", args);

    if (!ffprobeProcess.waitForFinished(10000)) {
        ffprobeProcess.kill();
        return makeUnexpected(TranscriptionError::AudioProcessingFailed);
    }

    if (ffprobeProcess.exitCode() != 0) {
        return makeUnexpected(TranscriptionError::AudioProcessingFailed);
    }

    QString output = ffprobeProcess.readAllStandardOutput();
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(output.toUtf8(), &error);

    if (error.error != QJsonParseError::NoError) {
        return makeUnexpected(TranscriptionError::AudioProcessingFailed);
    }

    QJsonObject root = doc.object();
    QJsonObject format = root["format"].toObject();
    double duration = format["duration"].toString().toDouble();

    return static_cast<qint64>(duration * 1000);  // Convert to milliseconds
}

Expected<bool, TranscriptionError> WhisperEngine::validateAudioFormat(const QString& audioFile) {
    QFileInfo fileInfo(audioFile);

    if (!fileInfo.exists()) {
        return makeUnexpected(TranscriptionError::InvalidAudioFormat);
    }

    QString suffix = fileInfo.suffix().toLower();
    QStringList supportedFormats = {"wav", "mp3", "flac", "m4a", "ogg"};

    if (!supportedFormats.contains(suffix)) {
        return makeUnexpected(TranscriptionError::InvalidAudioFormat);
    }

    return true;
}

QString WhisperEngine::getModelPath(const QString& modelSize) const {
    return modelsPath_ + "/ggml-" + modelSize + ".bin";
}

QString WhisperEngine::getModelUrl(const QString& modelSize) const {
    return WHISPER_CPP_REPO_URL + "/resolve/main/ggml-" + modelSize + ".bin";
}

Expected<bool, TranscriptionError> WhisperEngine::verifyModelIntegrity(const QString& modelPath) {
    QFileInfo fileInfo(modelPath);

    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return makeUnexpected(TranscriptionError::ModelNotLoaded);
    }

    if (fileInfo.size() < 1024 * 1024) {  // Models should be at least 1MiB
        return makeUnexpected(TranscriptionError::ModelNotLoaded);
    }

    return true;
}

Expected<TranscriptionResult, TranscriptionError> WhisperEngine::parseWhisperOutput(
    const QString& output,
    const TranscriptionSettings& settings) {

    if (settings.outputFormat == "json") {
        auto jsonResult = parseJsonOutput(output);
        if(jsonResult.hasError()) {
            return makeUnexpected(jsonResult.error());
        }
        // Parse JSON output from Whisper into TranscriptionResult
        QJsonObject jsonObj = jsonResult.value();
        TranscriptionResult result;
        
        // Extract basic information
        result.language = jsonObj.value("language").toString();
        result.detectedLanguage = jsonObj.value("language").toString();
        result.modelUsed = currentModel_;
        result.metadata = jsonObj;
        
        // Extract text from segments
        QJsonArray segments = jsonObj.value("segments").toArray();
        QString fullText;
        double totalConfidence = 0.0;
        int segmentCount = 0;
        
        for (const QJsonValue& segmentValue : segments) {
            QJsonObject segment = segmentValue.toObject();
            
            TranscriptionSegment seg;
            seg.startTime = static_cast<qint64>(segment.value("start").toDouble() * 1000); // Convert to milliseconds
            seg.endTime = static_cast<qint64>(segment.value("end").toDouble() * 1000);
            seg.text = segment.value("text").toString().trimmed();
            seg.confidence = segment.value("avg_logprob").toDouble();
            
            // Extract tokens if available
            QJsonArray tokens = segment.value("tokens").toArray();
            for (const QJsonValue& token : tokens) {
                seg.tokens.append(token.toString());
            }
            
            // Extract token probabilities if available
            QJsonArray tokenProbs = segment.value("token_logprobs").toArray();
            for (const QJsonValue& prob : tokenProbs) {
                seg.tokenProbabilities.append(prob.toDouble());
            }
            
            result.segments.append(seg);
            fullText += seg.text + " ";
            totalConfidence += seg.confidence;
            segmentCount++;
        }
        
        // Set overall results
        result.fullText = fullText.trimmed();
        result.confidence = segmentCount > 0 ? totalConfidence / segmentCount : 0.0;
        
        // If no segments available, try to get text directly
        if (result.fullText.isEmpty()) {
            result.fullText = jsonObj.value("text").toString();
        }
        
        Logger::instance().info("WhisperEngine: Successfully parsed JSON transcription result with {} segments", 
                               result.segments.size());
        
        return result;
    }

    // For other formats, create a basic result
    TranscriptionResult result;
    result.fullText = output.trimmed();
    result.language = settings.language;
    result.modelUsed = currentModel_;
    result.processingTime = 0;  // Would be calculated elsewhere

    return result;
}

Expected<QJsonObject, TranscriptionError> WhisperEngine::parseJsonOutput(const QString& jsonStr) {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &error);

    if (error.error != QJsonParseError::NoError) {
        Logger::instance().error("WhisperEngine: Failed to parse JSON output: {}", error.errorString().toStdString());
        return makeUnexpected(TranscriptionError::InferenceError);
    }

    return doc.object();
}

QString WhisperEngine::generateTaskId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QProcess* WhisperEngine::createWhisperProcess(const QString& workingDir) {
    auto* process = new QProcess(this);
    process->setWorkingDirectory(workingDir);

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &WhisperEngine::onWhisperProcessFinished);
    connect(process, &QProcess::errorOccurred,
            this, &WhisperEngine::onWhisperProcessError);
    connect(process, &QProcess::readyReadStandardOutput,
            this, &WhisperEngine::onWhisperProcessOutput);

    return process;
}

void WhisperEngine::cleanupTask(const QString& taskId) {
    QMutexLocker locker(&tasksMutex_);

    auto it = activeTasks_.find(taskId);
    if (it != activeTasks_.end()) {
        if (it->second->process) {
            it->second->process->deleteLater();
        }
        if (!it->second->tempDir.isEmpty()) {
            cleanupTempDirectory(it->second->tempDir);
        }
        activeTasks_.erase(it);
    }
}

Expected<QString, TranscriptionError> WhisperEngine::createTempDirectory() {
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        return makeUnexpected(TranscriptionError::ResourceExhausted);
    }

    tempDir.setAutoRemove(false);  // We'll clean up manually
    return tempDir.path();
}

void WhisperEngine::cleanupTempDirectory(const QString& tempDir) {
    QDir dir(tempDir);
    if (dir.exists()) {
        dir.removeRecursively();
    }
}

Expected<bool, TranscriptionError> WhisperEngine::extractAudioFromVideo(
    const QString& videoPath,
    const QString& audioPath) {

    QProcess ffmpegProcess;
    QStringList args;

    args << "-i" << videoPath;
    args << "-vn";  // No video
    args << "-ar" << QString::number(SAMPLE_RATE);
    args << "-ac" << QString::number(CHANNELS);
    args << "-c:a" << "pcm_s16le";
    args << "-y" << audioPath;

    ffmpegProcess.start("ffmpeg", args);

    if (!ffmpegProcess.waitForFinished(120000)) { // 2 minute timeout
        ffmpegProcess.kill();
        return makeUnexpected(TranscriptionError::AudioProcessingFailed);
    }

    if (ffmpegProcess.exitCode() != 0) {
        Logger::instance().error("WhisperEngine: Audio extraction failed: {}", ffmpegProcess.readAllStandardError().toStdString());
        return makeUnexpected(TranscriptionError::AudioProcessingFailed);
    }

    return true;
}

void WhisperEngine::onWhisperProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    auto* process = qobject_cast<QProcess*>(sender());
    if (!process) return;

    QMutexLocker locker(&tasksMutex_);

    // Find the task for this process
    QString taskId;
    for (const auto& pair : activeTasks_) {
        if (pair.second->process == process) {
            taskId = pair.first;
            break;
        }
    }

    if (taskId.isEmpty()) return;

    if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
        // Process completed successfully - results will be parsed by the calling thread
    } else {
        QString error = process->readAllStandardError();
        emit transcriptionFailed(taskId, TranscriptionError::InferenceError, error);
    }
}

void WhisperEngine::onWhisperProcessError(QProcess::ProcessError error) {
    Q_UNUSED(error);
    auto* process = qobject_cast<QProcess*>(sender());
    if (!process) return;

    QMutexLocker locker(&tasksMutex_);
    for (const auto& pair : activeTasks_) {
        if (pair.second->process == process) {
            emit transcriptionFailed(pair.first, TranscriptionError::InferenceError, "Process error occurred");
            break;
        }
    }
}

void WhisperEngine::onWhisperProcessOutput() {
    auto* process = qobject_cast<QProcess*>(sender());
    if (!process) return;

    // Read progress information and emit progress signals
    QByteArray data = process->readAllStandardError();
    QString output = QString::fromUtf8(data);

    // Find the task for this process
    QMutexLocker locker(&tasksMutex_);
    QString taskId;
    for (const auto& pair : activeTasks_) {
        if (pair.second->process == process) {
            taskId = pair.first;
            break;
        }
    }
    
    if (taskId.isEmpty()) {
        return; // No task found for this process
    }
    
    // Parse progress information from whisper output
    // Whisper outputs progress in different formats:
    // 1. Processing segments: [00:00.000 --> 00:05.000]
    // 2. Timestamp format: [12.50s -> 15.25s]
    // 3. Percentage format: "progress: 45.2%"
    
    double percentage = 0.0;
    QString currentSegment;
    
    // Parse timestamp-based progress
    QRegularExpression timestampRegex(TIMESTAMP_PATTERN);
    QRegularExpressionMatch timestampMatch = timestampRegex.match(output);
    if (timestampMatch.hasMatch()) {
        bool ok1, ok2;
        double startTime = timestampMatch.captured(1).toDouble(&ok1);
        double endTime = timestampMatch.captured(2).toDouble(&ok2);
        
        if (ok1 && ok2) {
            auto it = activeTasks_.find(taskId);
            if (it != activeTasks_.end()) {
                qint64 totalDuration = it->second->audioDuration;
                if (totalDuration > 0) {
                    percentage = (endTime * 1000.0) / totalDuration * 100.0;
                    currentSegment = QString("Processing audio segment %1s - %2s")
                                       .arg(startTime, 0, 'f', 2)
                                       .arg(endTime, 0, 'f', 2);
                }
            }
        }
    }
    
    // Parse segment-based progress
    QRegularExpression segmentRegex(SEGMENT_PATTERN);
    QRegularExpressionMatch segmentMatch = segmentRegex.match(output);
    if (segmentMatch.hasMatch()) {
        int startMin = segmentMatch.captured(1).toInt();
        int startSec = segmentMatch.captured(2).toInt();
        int startMs = segmentMatch.captured(3).toInt();
        int endMin = segmentMatch.captured(4).toInt();
        int endSec = segmentMatch.captured(5).toInt();
        int endMs = segmentMatch.captured(6).toInt();
        
        qint64 endTimeMs = (endMin * 60 + endSec) * 1000 + endMs;
        
        auto it = activeTasks_.find(taskId);
        if (it != activeTasks_.end()) {
            qint64 totalDuration = it->second->audioDuration;
            if (totalDuration > 0) {
                percentage = static_cast<double>(endTimeMs) / totalDuration * 100.0;
                currentSegment = QString("Processing segment %1:%2.%3 --> %4:%5.%6")
                                   .arg(startMin, 2, 10, QChar('0'))
                                   .arg(startSec, 2, 10, QChar('0'))
                                   .arg(startMs, 3, 10, QChar('0'))
                                   .arg(endMin, 2, 10, QChar('0'))
                                   .arg(endSec, 2, 10, QChar('0'))
                                   .arg(endMs, 3, 10, QChar('0'));
            }
        }
    }
    
    // Parse percentage-based progress
    QRegularExpression percentageRegex(R"(progress:\s*(\d+(?:\.\d+)?)%)");
    QRegularExpressionMatch percentageMatch = percentageRegex.match(output);
    if (percentageMatch.hasMatch()) {
        bool ok;
        double parsedPercentage = percentageMatch.captured(1).toDouble(&ok);
        if (ok && parsedPercentage >= 0.0 && parsedPercentage <= 100.0) {
            percentage = parsedPercentage;
            currentSegment = QString("Processing: %1%").arg(percentage, 0, 'f', 1);
        }
    }
    
    // Look for general status messages
    if (output.contains("whisper_full_with_state")) {
        currentSegment = "Running whisper inference";
    } else if (output.contains("load_model")) {
        currentSegment = "Loading model";
    } else if (output.contains("processing")) {
        currentSegment = "Processing audio";
    }
    
    // Update progress if we parsed something useful
    if (percentage > 0.0 || !currentSegment.isEmpty()) {
        // Ensure percentage is within valid range
        percentage = qBound(0.0, percentage, 100.0);
        
        auto it = activeTasks_.find(taskId);
        if (it != activeTasks_.end()) {
            // Update the current segment in the task
            TranscriptionProgress progress = createProgressInfo(*it->second, percentage);
            if (!currentSegment.isEmpty()) {
                progress.currentSegment = currentSegment;
            }
            
            locker.unlock(); // Unlock before emitting signal
            emit transcriptionProgress(taskId, progress);
        }
    }
    
    // Also log the output for debugging
    if (!output.trimmed().isEmpty()) {
        Logger::instance().debug("WhisperEngine: Process output: {}", output.trimmed().toStdString());
    }
}

// Language detection implementation
QFuture<Expected<QString, TranscriptionError>> WhisperEngine::detectLanguage(const QString& audioFilePath) {
    return QtConcurrent::run([this, audioFilePath]() -> Expected<QString, TranscriptionError> {
        if (!isInitialized_ || currentModel_.isEmpty()) {
            return makeUnexpected(TranscriptionError::ModelNotLoaded);
        }

        if (!InputValidator::isValidMediaFile(audioFilePath)) {
            return makeUnexpected(TranscriptionError::InvalidAudioFormat);
        }

        // Load audio data
        auto audioDataResult = whisperWrapper_->loadAudioFile(audioFilePath);
        if (audioDataResult.hasError()) {
            return makeUnexpected(convertWhisperError(audioDataResult.error()));
        }

        // Use whisper wrapper to detect language
        auto languageResult = whisperWrapper_->detectLanguage(audioDataResult.value());
        if (languageResult.hasError()) {
            Logger::instance().error("WhisperEngine: Language detection failed: {}", static_cast<int>(languageResult.error()));
            return makeUnexpected(convertWhisperError(languageResult.error()));
        }

        Logger::instance().info("WhisperEngine: Detected language: {}", languageResult.value().toStdString());
        return languageResult.value();
    });
}

Expected<QString, TranscriptionError> WhisperEngine::convertToSRT(const TranscriptionResult& result) {
    auto formatResult = TranscriptionFormatter::convertToSRT(result);
    if (formatResult.hasError()) {
        return makeUnexpected(TranscriptionError::AudioProcessingFailed);
    }
    return formatResult.value();
}

Expected<QString, TranscriptionError> WhisperEngine::convertToVTT(const TranscriptionResult& result) {
    auto formatResult = TranscriptionFormatter::convertToVTT(result);
    if (formatResult.hasError()) {
        return makeUnexpected(TranscriptionError::AudioProcessingFailed);
    }
    return formatResult.value();
}

Expected<QString, TranscriptionError> WhisperEngine::convertToPlainText(const TranscriptionResult& result) {
    auto formatResult = TranscriptionFormatter::convertToPlainText(result);
    if (formatResult.hasError()) {
        return makeUnexpected(TranscriptionError::AudioProcessingFailed);
    }
    return formatResult.value();
}

void WhisperEngine::setMaxConcurrentTranscriptions(int maxTasks) {
    maxConcurrentTranscriptions_ = qMax(1, maxTasks);
}

void WhisperEngine::setMemoryLimit(qint64 maxMemoryMB) {
    maxMemoryMB_ = qMax(512LL, maxMemoryMB);
}

void WhisperEngine::setGPUEnabled(bool enabled) {
    gpuEnabled_ = enabled;
}

void WhisperEngine::setModelCacheSize(int maxModels) {
    maxModelCache_ = qMax(1, maxModels);
}

QJsonObject WhisperEngine::getPerformanceStats() const {
    QJsonObject stats;
    stats["totalTranscriptions"] = static_cast<qint64>(performanceStats_.totalTranscriptions);
    stats["totalProcessingTime"] = static_cast<qint64>(performanceStats_.totalProcessingTime);
    stats["totalAudioDuration"] = static_cast<qint64>(performanceStats_.totalAudioDuration);
    stats["averageRealTimeFactor"] = performanceStats_.averageRealTimeFactor;
    stats["lastReset"] = performanceStats_.lastReset.toString(Qt::ISODate);
    return stats;
}

void WhisperEngine::clearPerformanceStats() {
    performanceStats_ = PerformanceStats{};
    performanceStats_.lastReset = QDateTime::currentDateTime();
}

// Helper method for progress parsing
TranscriptionProgress WhisperEngine::createProgressInfo(const TranscriptionTask& task, double percentage) {
    TranscriptionProgress progress;
    progress.taskId = task.taskId;
    progress.audioFile = task.audioFile;
    progress.percentage = percentage;
    progress.processedDuration = static_cast<qint64>(percentage * task.audioDuration / 100.0);
    progress.totalDuration = task.audioDuration;
    progress.elapsedTime = QDateTime::currentMSecsSinceEpoch() - task.startTime;
    progress.isCompleted = (percentage >= 100.0);
    progress.isCancelled = task.isCancelled;

    if (progress.elapsedTime > 0 && percentage > 0) {
        progress.estimatedTimeRemaining = static_cast<qint64>(
            (progress.elapsedTime / percentage) * (100.0 - percentage)
        );
    } else {
        progress.estimatedTimeRemaining = 0;
    }

    return progress;
}

void WhisperEngine::updateTaskProgress(const QString& taskId, double percentage) {
    QMutexLocker locker(&tasksMutex_);

    auto it = activeTasks_.find(taskId);
    if (it != activeTasks_.end()) {
        TranscriptionProgress progress = createProgressInfo(*it->second, percentage);
        emit transcriptionProgress(taskId, progress);
    }
}

bool WhisperEngine::checkResourceLimits() const {
    // Check concurrent transcription limit
    if (activeTasks_.size() >= static_cast<size_t>(maxConcurrentTranscriptions_)) {
        return false;
    }

    // Check actual memory usage
    qint64 currentMemoryUsage = getCurrentMemoryUsage();
    qint64 estimatedAdditionalMemory = 0;
    
    // Estimate memory needed for active tasks
    for (const auto& pair : activeTasks_) {
        // Estimate memory usage based on audio duration and model size
        // Whisper models typically require 2-4x the model size in RAM during processing
        qint64 modelMemory = getModelMemoryRequirement(currentModel_);
        qint64 audioMemory = pair.second->audioDuration / 1024; // ~1MB per second of audio
        estimatedAdditionalMemory += modelMemory + audioMemory;
    }
    
    qint64 totalEstimatedMemory = currentMemoryUsage + estimatedAdditionalMemory;
    
    if (totalEstimatedMemory > (maxMemoryMB_ * 1024 * 1024)) {
        Logger::instance().warn("WhisperEngine: Memory limit exceeded - Current: {}MB, Estimated: {}MB, Limit: {}MB",
                               currentMemoryUsage / (1024 * 1024),
                               totalEstimatedMemory / (1024 * 1024),
                               maxMemoryMB_);
        return false;
    }

    return true;
}

Expected<QString, TranscriptionError> WhisperEngine::startRealtimeTranscription(const TranscriptionSettings& settings) {
    if (!isInitialized_ || currentModel_.isEmpty()) {
        return makeUnexpected(TranscriptionError::ModelNotLoaded);
    }

    QMutexLocker locker(&tasksMutex_);

    if (realtimeSessions_.size() >= static_cast<size_t>(maxConcurrentTranscriptions_)) {
        return makeUnexpected(TranscriptionError::ResourceExhausted);
    }


    QString sessionId = generateSessionId();
    auto session = std::make_unique<RealtimeSession>();
    session->sessionId = sessionId;
    session->settings = settings;
    session->isActive = true;
    session->isMicrophoneSession = false;
    session->sessionStartTime = QDateTime::currentDateTime();

    auto setupResult = setupRealtimeSession(session.get());
    if (setupResult.hasError()) {
        return makeUnexpected(setupResult.error());
    }

    realtimeSessions_[sessionId] = std::move(session);

    emit realtimeTranscriptionStarted(sessionId);
    Logger::instance().info("WhisperEngine: Started realtime transcription session: {}", sessionId.toStdString());

    return sessionId;
}

Expected<bool, TranscriptionError> WhisperEngine::feedAudioData(const QString& sessionId, const QByteArray& audioData) {
    QMutexLocker locker(&tasksMutex_);

    auto it = realtimeSessions_.find(sessionId);
    if (it == realtimeSessions_.end()) {
        return makeUnexpected(TranscriptionError::InvalidAudioFormat);
    }

    RealtimeSession* session = it->second.get();
    if (!session->isActive) {
        return makeUnexpected(TranscriptionError::InvalidAudioFormat);
    }

    // Check buffer size limits
    if (session->audioBuffer.size() + audioData.size() > MAX_BUFFER_SIZE) {
        Logger::instance().warn("WhisperEngine: Audio buffer overflow for session {}, dropping old data", sessionId.toStdString());
        session->audioBuffer.clear();
        session->lastProcessedPosition = 0;
    }

    // Append new audio data
    session->audioBuffer.append(audioData);

    // Calculate volume level for monitoring
    session->currentVolume = calculateVolumeLevel(audioData);
    emit microphoneVolumeChanged(sessionId, session->currentVolume);
    emit audioBufferStatus(sessionId, session->audioBuffer.size(), MAX_BUFFER_SIZE);

    return true;
}

Expected<QString, TranscriptionError> WhisperEngine::startMicrophoneTranscription(const TranscriptionSettings& settings) {
    if (!isInitialized_ || currentModel_.isEmpty()) {
        return makeUnexpected(TranscriptionError::ModelNotLoaded);
    }

    QMutexLocker locker(&tasksMutex_);

    if (realtimeSessions_.size() >= static_cast<size_t>(maxConcurrentTranscriptions_)) {
        return makeUnexpected(TranscriptionError::ResourceExhausted);
    }

    QString sessionId = generateSessionId();
    auto session = std::make_unique<RealtimeSession>();
    session->sessionId = sessionId;
    session->settings = settings;
    session->isActive = true;
    session->isMicrophoneSession = true;
    session->sessionStartTime = QDateTime::currentDateTime();

    auto setupResult = setupRealtimeSession(session.get());
    if (setupResult.hasError()) {
        return makeUnexpected(setupResult.error());
    }

    auto micResult = setupMicrophoneCapture(session.get());
    if (micResult.hasError()) {
        return makeUnexpected(micResult.error());
    }

    realtimeSessions_[sessionId] = std::move(session);

    emit realtimeTranscriptionStarted(sessionId);
    Logger::instance().info("WhisperEngine: Started microphone transcription session: {}", sessionId.toStdString());

    return sessionId;
}

Expected<bool, TranscriptionError> WhisperEngine::stopRealtimeTranscription(const QString& sessionId) {
    QMutexLocker locker(&tasksMutex_);

    auto it = realtimeSessions_.find(sessionId);
    if (it == realtimeSessions_.end()) {
        return makeUnexpected(TranscriptionError::InvalidAudioFormat);
    }

    cleanupRealtimeSession(sessionId);
    realtimeSessions_.erase(it);

    emit realtimeTranscriptionStopped(sessionId);
    Logger::instance().info("WhisperEngine: Stopped realtime transcription session: {}", sessionId.toStdString());

    return true;
}

Expected<bool, TranscriptionError> WhisperEngine::stopMicrophoneTranscription(const QString& sessionId) {
    return stopRealtimeTranscription(sessionId);
}

// Real-time processing slots

void WhisperEngine::onRealtimeAudioReady() {
    QBuffer* buffer = qobject_cast<QBuffer*>(sender());
    if (!buffer) return;

    QMutexLocker locker(&tasksMutex_);

    // Find the session for this buffer
    for (const auto& pair : realtimeSessions_) {
        if (pair.second->audioBufferDevice == buffer) {
            feedAudioData(pair.first, buffer->data());
            break;
        }
    }
}

void WhisperEngine::onRealtimeProcessingTimer() {
    QTimer* timer = qobject_cast<QTimer*>(sender());
    if (!timer) return;

    QMutexLocker locker(&tasksMutex_);

    // Find the session for this timer
    for (const auto& pair : realtimeSessions_) {
        if (pair.second->processingTimer == timer) {
            processRealtimeAudio(pair.second.get());
            break;
        }
    }
}

void WhisperEngine::onMicrophoneStateChanged(QAudio::State state) {
    if (state == QAudio::StoppedState) {
        Logger::instance().warn("WhisperEngine: Microphone capture stopped unexpectedly");
    }
}

// Helper method implementations

Expected<bool, TranscriptionError> WhisperEngine::setupRealtimeSession(RealtimeSession* session) {
    // Create temporary directory
    auto tempResult = createTempDirectory();
    if (tempResult.hasError()) {
        return makeUnexpected(tempResult.error());
    }
    session->tempDir = tempResult.value();

    // Initialize audio buffer
    session->audioBuffer.clear();
    session->audioBufferDevice = new QBuffer(&session->audioBuffer, this);
    session->audioBufferDevice->open(QIODevice::ReadWrite);

    // Set up processing timer
    session->processingTimer = new QTimer(this);
    session->processingTimer->setInterval(REALTIME_PROCESSING_INTERVAL);
    connect(session->processingTimer, &QTimer::timeout,
            this, &WhisperEngine::onRealtimeProcessingTimer);
    session->processingTimer->start();

    // Initialize processing state
    session->lastProcessedPosition = 0;
    session->segmentStartTime = QDateTime::currentMSecsSinceEpoch();
    session->totalAudioProcessed = 0;
    session->currentVolume = 0.0;

    return true;
}

Expected<bool, TranscriptionError> WhisperEngine::setupMicrophoneCapture(RealtimeSession* session) {
    // Set up audio format for Whisper (16kHz, 16-bit, mono)
    session->audioFormat.setSampleRate(SAMPLE_RATE);
    session->audioFormat.setChannelCount(CHANNELS);
    session->audioFormat.setSampleFormat(QAudioFormat::Int16);

    // Get default audio input device
    auto audioDevices = QMediaDevices::audioInputs();
    if (audioDevices.isEmpty()) {
        Logger::instance().error("WhisperEngine: No audio input devices available");
        return makeUnexpected(TranscriptionError::AudioProcessingFailed);
    }

    QAudioDevice audioDevice = QMediaDevices::defaultAudioInput();

    // Create audio input
    session->audioInput = new QAudioSource(audioDevice, session->audioFormat, this);
    session->audioInput->setBufferSize(REALTIME_BUFFER_SIZE * sizeof(qint16));

    connect(session->audioInput, &QAudioSource::stateChanged,
            this, &WhisperEngine::onMicrophoneStateChanged);

    // Start capturing
    session->audioInput->start(session->audioBufferDevice);

    Logger::instance().info("WhisperEngine: Started microphone capture for session {}", session->sessionId.toStdString());
    return true;
}

QString WhisperEngine::generateSessionId() {
    return "rt_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

double WhisperEngine::calculateVolumeLevel(const QByteArray& audioData) {
    if (audioData.isEmpty()) return 0.0;

    const qint16* samples = reinterpret_cast<const qint16*>(audioData.constData());
    int sampleCount = audioData.size() / sizeof(qint16);

    double sum = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        sum += abs(samples[i]);
    }

    double average = sum / sampleCount;
    return average / 32768.0; // Normalize to 0.0-1.0 range
}

void WhisperEngine::cleanupRealtimeSession(const QString& sessionId) {
    auto it = realtimeSessions_.find(sessionId);
    if (it == realtimeSessions_.end()) return;

    RealtimeSession* session = it->second.get();

    if (session->processingTimer) {
        session->processingTimer->stop();
        session->processingTimer->deleteLater();
    }

    if (session->audioInput) {
        session->audioInput->stop();
        session->audioInput->deleteLater();
    }

    if (session->audioBufferDevice) {
        session->audioBufferDevice->close();
        session->audioBufferDevice->deleteLater();
    }

    if (!session->tempDir.isEmpty()) {
        cleanupTempDirectory(session->tempDir);
    }
}

Expected<bool, TranscriptionError> WhisperEngine::processRealtimeAudio(RealtimeSession* session) {
    if (!session->isActive || session->audioBuffer.size() <= session->lastProcessedPosition) {
        return true; // Nothing to process
    }

    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

    // Check if we should process a segment
    if (!shouldProcessSegment(session, currentTime)) {
        return true;
    }

    // Extract audio segment for processing
    qint64 bytesToProcess = session->audioBuffer.size() - session->lastProcessedPosition;
    QByteArray audioSegment = session->audioBuffer.mid(session->lastProcessedPosition, bytesToProcess);

    // Convert to float array for Whisper
    std::vector<float> audioData = convertBytesToFloat(audioSegment);

    if (audioData.size() < SAMPLE_RATE) { // Less than 1 second of audio
        return true; // Wait for more audio
    }

    // Create Whisper configuration
    WhisperConfig config;
    config.language = session->settings.language == "auto" ? "" : session->settings.language;
    config.enableTimestamps = session->settings.enableTimestamps;
    config.enableTokenTimestamps = session->settings.enableWordConfidence;
    config.temperature = session->settings.temperature;
    config.nThreads = QThread::idealThreadCount();

    // Perform transcription
    auto result = whisperWrapper_->transcribe(audioData, config);
    if (result.hasError()) {
        Logger::instance().warn("WhisperEngine: Realtime transcription failed for session {}: {}",
                                session->sessionId.toStdString(), static_cast<int>(result.error()));
        return makeUnexpected(convertWhisperError(result.error()));
    }

    // Process results and emit segments
    WhisperResult whisperResult = result.value();
    for (const auto& whisperSegment : whisperResult.segments) {
        TranscriptionSegment segment = convertWhisperSegment(whisperSegment);

        // Adjust timestamps to account for session start time
        qint64 sessionOffset = session->segmentStartTime;
        segment.startTime += sessionOffset;
        segment.endTime += sessionOffset;

        emit realtimeSegmentReady(session->sessionId, segment);
    }

    // Update processing position
    session->lastProcessedPosition = session->audioBuffer.size();
    session->totalAudioProcessed += audioData.size();

    return true;
}

std::vector<float> WhisperEngine::convertBytesToFloat(const QByteArray& audioData) {
    const qint16* samples = reinterpret_cast<const qint16*>(audioData.constData());
    int sampleCount = audioData.size() / sizeof(qint16);

    std::vector<float> floatData;
    floatData.reserve(sampleCount);

    for (int i = 0; i < sampleCount; ++i) {
        floatData.push_back(static_cast<float>(samples[i]) / 32768.0f);
    }

    return floatData;
}

bool WhisperEngine::shouldProcessSegment(RealtimeSession* session, qint64 currentTime) {
    qint64 timeSinceLastSegment = currentTime - session->segmentStartTime;
    qint64 audioBufferDuration = (session->audioBuffer.size() / sizeof(qint16)) * 1000 / SAMPLE_RATE;

    // Process if we have enough audio or enough time has passed
    return (audioBufferDuration >= REALTIME_SEGMENT_LENGTH) ||
           (timeSinceLastSegment >= REALTIME_SEGMENT_LENGTH && audioBufferDuration >= MIN_AUDIO_LENGTH);
}

TranscriptionResult WhisperEngine::convertWhisperResult(const WhisperResult& whisperResult, const TranscriptionSettings& settings) {
    TranscriptionResult result;
    result.language = whisperResult.language;
    result.fullText = whisperResult.fullText;
    result.confidence = whisperResult.avgConfidence;
    result.processingTime = static_cast<qint64>(whisperResult.processingTime * 1000); // to ms
    result.modelUsed = settings.modelSize;

    for (const auto& whisperSeg : whisperResult.segments) {
        result.segments.append(convertWhisperSegment(whisperSeg));
    }
    return result;
}

TranscriptionSegment WhisperEngine::convertWhisperSegment(const WhisperSegment& whisperSegment) {
    TranscriptionSegment segment;
    segment.startTime = static_cast<qint64>(whisperSegment.startTime * 1000); // Convert to milliseconds
    segment.endTime = static_cast<qint64>(whisperSegment.endTime * 1000);
    segment.text = whisperSegment.text;
    segment.confidence = whisperSegment.confidence;

    // Convert word-level data
    for (const auto& wordPair : whisperSegment.words) {
        segment.tokens.append(wordPair.first);
        segment.tokenProbabilities.append(wordPair.second);
    }

    return segment;
}

TranscriptionError WhisperEngine::convertWhisperError(WhisperError error) {
    switch (error) {
        case WhisperError::InitializationFailed:
            return TranscriptionError::ModelNotLoaded;
        case WhisperError::ModelLoadFailed:
            return TranscriptionError::ModelNotLoaded;
        case WhisperError::AudioProcessingFailed:
            return TranscriptionError::AudioProcessingFailed;
        case WhisperError::InferenceFailed:
            return TranscriptionError::InferenceError;
        case WhisperError::InvalidInput:
            return TranscriptionError::InvalidAudioFormat;
        case WhisperError::OutOfMemory:
            return TranscriptionError::ResourceExhausted;
        case WhisperError::InvalidModel:
            return TranscriptionError::ModelNotLoaded;
        case WhisperError::UnsupportedFeature:
            return TranscriptionError::UnsupportedLanguage;
        case WhisperError::Cancelled:
            return TranscriptionError::Cancelled;
        default:
            return TranscriptionError::InferenceError;
    }
}

// Memory monitoring helper methods
qint64 WhisperEngine::getCurrentMemoryUsage() const {
    // Get current process memory usage using Qt's platform-specific methods
#ifdef Q_OS_WIN
    // Windows implementation using GetProcessMemoryInfo
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<qint64>(pmc.WorkingSetSize);
    }
#elif defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    // Unix-like systems: read from /proc/self/status or use getrusage
    QFile statusFile("/proc/self/status");
    if (statusFile.open(QIODevice::ReadOnly)) {
        QTextStream stream(&statusFile);
        QString line;
        while (stream.readLineInto(&line)) {
            if (line.startsWith("VmRSS:")) {
                // Extract memory value in kB and convert to bytes
                QStringList parts = line.split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    bool ok;
                    qint64 memoryKB = parts[1].toLongLong(&ok);
                    if (ok) {
                        return memoryKB * 1024; // Convert kB to bytes
                    }
                }
                break;
            }
        }
    }
    
    // Fallback: use rusage for Unix systems
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef Q_OS_MACOS
        return static_cast<qint64>(usage.ru_maxrss); // macOS returns bytes
#else
        return static_cast<qint64>(usage.ru_maxrss * 1024); // Linux returns kB
#endif
    }
#endif
    
    // Fallback: return 0 if unable to determine memory usage
    Logger::instance().warn("WhisperEngine: Unable to determine current memory usage");
    return 0;
}

qint64 WhisperEngine::getModelMemoryRequirement(const QString& modelSize) const {
    // Base memory requirement from MODEL_SIZES
    qint64 baseSize = MODEL_SIZES.value(modelSize, 0);
    if (baseSize == 0) {
        Logger::instance().warn("WhisperEngine: Unknown model size: {}", modelSize.toStdString());
        return 512 * 1024 * 1024; // Default to 512MB
    }
    
    // Whisper models typically require 2-3x their size in memory during processing
    // due to intermediate tensors, activations, and GPU memory (if used)
    qint64 processingMultiplier = gpuEnabled_ ? 3 : 2; // GPU needs more memory
    qint64 totalMemoryRequired = baseSize * processingMultiplier;
    
    // Add extra memory for audio buffers and processing overhead
    qint64 overhead = 256 * 1024 * 1024; // 256MB overhead
    
    return totalMemoryRequired + overhead;
}

} // namespace Murmur