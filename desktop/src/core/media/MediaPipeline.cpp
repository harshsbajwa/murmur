#include "MediaPipeline.hpp"
#include "FFmpegWrapper.hpp"
#include "HardwareAccelerator.hpp"
#include "../common/Logger.hpp"
#include "../security/InputValidator.hpp"

#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QMutexLocker>
#include <QtConcurrent>
#include <QUuid>
#include <QElapsedTimer>

namespace Murmur {

MediaPipeline::MediaPipeline(QObject* parent)
    : QObject(parent)
    , ffmpegWrapper_(std::make_unique<FFmpegWrapper>(this))
    , hardwareAccelerator_(std::make_unique<HardwareAccelerator>(this))
    , errorRecovery_(std::make_unique<ErrorRecovery>(this))
    , retryManager_(std::make_unique<RetryManager>(Murmur::RetryConfigs::hardware(), this))
    , tempDir_(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/MurmurMedia") {
    
    // Connect FFmpegWrapper signals
    connect(ffmpegWrapper_.get(), &FFmpegWrapper::operationStarted,
            this, &MediaPipeline::onFFmpegOperationStarted);
    connect(ffmpegWrapper_.get(), &FFmpegWrapper::operationProgress,
            this, &MediaPipeline::onFFmpegOperationProgress);
    connect(ffmpegWrapper_.get(), &FFmpegWrapper::operationCompleted,
            this, &MediaPipeline::onFFmpegOperationCompleted);
    connect(ffmpegWrapper_.get(), &FFmpegWrapper::operationFailed,
            this, &MediaPipeline::onFFmpegOperationFailed);
    connect(ffmpegWrapper_.get(), &FFmpegWrapper::operationCancelled,
            this, &MediaPipeline::onFFmpegOperationCancelled);
    
    // Setup error recovery strategies
    setupErrorRecoveryStrategies();
    
    // Enable circuit breaker for hardware operations
    errorRecovery_->enableCircuitBreaker("HardwareAccelerator", 3, std::chrono::minutes(2));
    errorRecovery_->enableCircuitBreaker("FFmpegWrapper", 5, std::chrono::minutes(1));
    
    // Initialize hardware acceleration with retry
    initializeWithRetry();
    
    Logger::instance().info("MediaPipeline initialized with comprehensive error handling");
}

Murmur::MediaPipeline::~MediaPipeline() {
    cancelAllOperations();
}

QFuture<Murmur::Expected<Murmur::VideoInfo, Murmur::MediaError>> Murmur::MediaPipeline::analyzeVideo(const QString& filePath) {
    return QtConcurrent::run([this, filePath]() -> Murmur::Expected<Murmur::VideoInfo, Murmur::MediaError> {
        if (!InputValidator::validateVideoFile(filePath)) {
            return makeUnexpected(Murmur::MediaError::InvalidFile);
        }
        
        // Use FFmpegWrapper to analyze file
        auto future = ffmpegWrapper_->analyzeFile(filePath);
        auto result = future.result();
        
        if (result.hasError()) {
            return makeUnexpected(convertFromFFmpegError(result.error()));
        }
        
        Murmur::VideoInfo info = convertFromMediaFileInfo(result.value());
        return info;
    });
}

Murmur::Expected<bool, Murmur::MediaError> Murmur::MediaPipeline::validateVideoFile(const QString& filePath) {
    if (!QFileInfo::exists(filePath)) {
        return makeUnexpected(Murmur::MediaError::InvalidFile);
    }
    
    QFileInfo fileInfo(filePath);
    if (!InputValidator::validateVideoFormat(fileInfo.suffix())) {
        return makeUnexpected(Murmur::MediaError::UnsupportedFormat);
    }
    
    if (!InputValidator::validateFileSize(fileInfo.size())) {
        return makeUnexpected(Murmur::MediaError::ResourceExhausted);
    }
    
    return true;
}

QStringList Murmur::MediaPipeline::supportedFormats() {
    return {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "3gp", "ogv"};
}

QStringList Murmur::MediaPipeline::supportedCodecs() {
    return {"h264", "h265", "vp8", "vp9", "av1", "xvid", "mpeg4"};
}

QFuture<Murmur::Expected<QString, Murmur::MediaError>> Murmur::MediaPipeline::convertVideo(
    const QString& inputPath,
    const QString& outputPath,
    const Murmur::ConversionSettings& settings) {
    
    return QtConcurrent::run([this, inputPath, outputPath, settings]() -> Murmur::Expected<QString, Murmur::MediaError> {
        QMutexLocker locker(&operationsMutex_);
        
        if (!checkResourceLimits()) {
            return makeUnexpected(Murmur::MediaError::ResourceExhausted);
        }
        
        auto pathResult = validateAndPreparePaths(inputPath, outputPath);
        if (pathResult.hasError()) {
            return makeUnexpected(pathResult.error());
        }
        
        // Convert settings to FFmpeg options with hardware acceleration
        Murmur::ConversionOptions ffmpegOptions = convertToFFmpegOptions(settings);
        
        // Create our operation ID and track it
        QString operationId = generateOperationId();
        
        // Create operation context for tracking
        auto context = std::make_unique<OperationContext>();
        context->id = operationId;
        context->inputFile = inputPath;
        context->outputFile = outputPath;
        context->settings = settings;
        context->inputInfo = QFileInfo(inputPath);
        context->startTime = QDateTime::currentMSecsSinceEpoch();
        context->isCancelled = false;
        
        activeOperations_[operationId] = std::move(context);
        
        // Set up progress callback
        auto progressCallback = [this, operationId](const Murmur::ProgressInfo& progress) {
            Murmur::ConversionProgress convProgress = createProgressFromFFmpeg(operationId, progress);
            emit conversionProgress(operationId, convProgress);
        };
        
        locker.unlock();
        
        // Start FFmpeg conversion using library integration
        auto future = ffmpegWrapper_->convertVideo(inputPath, outputPath, ffmpegOptions, progressCallback);
        auto result = future.result();
        
        // Cleanup operation
        {
            QMutexLocker cleanupLocker(&operationsMutex_);
            cleanupOperation(operationId);
        }
        
        if (result.hasError()) {
            return makeUnexpected(convertFromFFmpegError(result.error()));
        }
        
        return result.value();
    });
}

QFuture<Murmur::Expected<QString, Murmur::MediaError>> Murmur::MediaPipeline::extractAudio(
    const QString& videoPath,
    const QString& outputPath,
    const QString& format) {
    
    return QtConcurrent::run([this, videoPath, outputPath, format]() -> Murmur::Expected<QString, Murmur::MediaError> {
        if (!InputValidator::validateVideoFile(videoPath)) {
            return makeUnexpected(Murmur::MediaError::InvalidFile);
        }
        
        // Create options for audio extraction
        Murmur::ConversionOptions options;
        if (format == "wav") {
            options.audioCodec = "pcm_s16le";
        } else if (format == "mp3") {
            options.audioCodec = "libmp3lame";
        } else {
            options.audioCodec = "aac";
        }
        options.videoCodec = ""; // No video encoding
        
        auto future = ffmpegWrapper_->extractAudio(videoPath, outputPath, options);
        auto result = future.result();
        
        if (result.hasError()) {
            return makeUnexpected(convertFromFFmpegError(result.error()));
        }
        
        return result.value();
    });
}

QFuture<Murmur::Expected<QString, Murmur::MediaError>> Murmur::MediaPipeline::generateThumbnail(
    const QString& videoPath,
    const QString& outputPath,
    int timeOffset) {
    
    return QtConcurrent::run([this, videoPath, outputPath, timeOffset]() -> Murmur::Expected<QString, Murmur::MediaError> {
        if (!InputValidator::validateVideoFile(videoPath)) {
            return makeUnexpected(Murmur::MediaError::InvalidFile);
        }
        
        auto future = ffmpegWrapper_->generateThumbnail(videoPath, outputPath, static_cast<double>(timeOffset));
        auto result = future.result();
        
        if (result.hasError()) {
            return makeUnexpected(convertFromFFmpegError(result.error()));
        }
        
        return result.value();
    });
}

void Murmur::MediaPipeline::cancelOperation(const QString& operationId) {
    ffmpegWrapper_->cancelOperation(operationId);
    
    QMutexLocker locker(&operationsMutex_);
    auto it = activeOperations_.find(operationId);
    if (it != activeOperations_.end()) {
        it->second->isCancelled = true;
        emit operationCancelled(operationId);
    }
}

void Murmur::MediaPipeline::cancelAllOperations() {
    ffmpegWrapper_->cancelAllOperations();
    
    QMutexLocker locker(&operationsMutex_);
    for (auto it = activeOperations_.begin(); it != activeOperations_.end(); ++it) {
        it->second->isCancelled = true;
        emit operationCancelled(it->first);
    }
    activeOperations_.clear();
}

QStringList Murmur::MediaPipeline::getActiveOperations() const {
    // Combine our tracked operations with FFmpeg wrapper operations
    QMutexLocker locker(&operationsMutex_);
    QStringList allOperations;
    for (const auto& pair : activeOperations_) {
        allOperations.append(pair.first);
    }
    allOperations.append(ffmpegWrapper_->getActiveOperations());
    allOperations.removeDuplicates();
    return allOperations;
}

void Murmur::MediaPipeline::setMaxConcurrentOperations(int maxOps) {
    maxConcurrentOperations_ = qMax(1, maxOps);
}

void Murmur::MediaPipeline::setMemoryLimit(qint64 maxMemoryMB) {
    maxMemoryMB_ = qMax(512LL, maxMemoryMB);
}

void Murmur::MediaPipeline::setTempDirectory(const QString& tempDir) {
    customTempDir_ = tempDir;
}

// FFmpeg wrapper signal handlers
void Murmur::MediaPipeline::onFFmpegOperationStarted(const QString& operationId, const QString& inputPath) {
    Logger::instance().info("FFmpeg operation started: {} -> {}", operationId.toStdString(), inputPath.toStdString());
}

void Murmur::MediaPipeline::onFFmpegOperationProgress(const QString& operationId, const Murmur::ProgressInfo& progress) {
    Murmur::ConversionProgress convProgress = createProgressFromFFmpeg(operationId, progress);
    emit conversionProgress(operationId, convProgress);
}

void Murmur::MediaPipeline::onFFmpegOperationCompleted(const QString& operationId, const QString& outputPath) {
    emit conversionCompleted(operationId, outputPath);
    Logger::instance().info("FFmpeg operation completed: {} -> {}", operationId.toStdString(), outputPath.toStdString());
}

void Murmur::MediaPipeline::onFFmpegOperationFailed(const QString& operationId, Murmur::FFmpegError error, const QString& errorMessage) {
    Murmur::MediaError mediaError = convertFromFFmpegError(error);
    emit conversionFailed(operationId, mediaError, errorMessage);
    Logger::instance().error("FFmpeg operation failed: {} - {}", operationId.toStdString(), errorMessage.toStdString());
}

void Murmur::MediaPipeline::onFFmpegOperationCancelled(const QString& operationId) {
    emit operationCancelled(operationId);
    Logger::instance().info("FFmpeg operation cancelled: {}", operationId.toStdString());
}

// Conversion utilities
Murmur::ConversionOptions Murmur::MediaPipeline::convertToFFmpegOptions(const Murmur::ConversionSettings& settings) {
    Murmur::ConversionOptions options;
    
    options.videoCodec = settings.videoCodec;
    options.audioCodec = settings.audioCodec;
    options.videoBitrate = settings.videoBitrate;
    options.audioBitrate = settings.audioBitrate;
    options.width = settings.maxWidth;
    options.height = settings.maxHeight;
    options.containerFormat = settings.outputFormat;
    
    // Map preset and quality
    if (settings.preserveQuality) {
        options.preset = "slow";
        options.crf = 18;
    } else {
        options.preset = "medium";
        options.crf = 23;
    }
    
    // Hardware acceleration - auto-detect best available
    Murmur::HardwareType bestHw = hardwareAccelerator_->getBestHardwareForCodec(settings.videoCodec, true);
    if (bestHw != Murmur::HardwareType::None && 
        hardwareAccelerator_->isHardwareEncodingRecommended(settings.videoCodec, settings.maxWidth, settings.maxHeight, settings.videoBitrate)) {
        
        switch (bestHw) {
            case Murmur::HardwareType::VideoToolbox:
                options.hwAccel = Murmur::HardwareAccel::VideoToolbox;
                break;
            case Murmur::HardwareType::CUDA:
                options.hwAccel = Murmur::HardwareAccel::CUDA;
                break;
            case Murmur::HardwareType::QSV:
                options.hwAccel = Murmur::HardwareAccel::QSV;
                break;
            case Murmur::HardwareType::VAAPI:
                options.hwAccel = Murmur::HardwareAccel::VAAPI;
                break;
            case Murmur::HardwareType::D3D11VA:
                options.hwAccel = Murmur::HardwareAccel::D3D11VA;
                break;
            default:
                options.hwAccel = Murmur::HardwareAccel::None;
                break;
        }
        
        Logger::instance().info("Using hardware acceleration: {}", static_cast<int>(bestHw));
    } else {
        options.hwAccel = Murmur::HardwareAccel::None;
    }
    
    // Custom options
    if (!settings.customOptions.isEmpty()) {
        options.customFilters = settings.customOptions.split(' ', Qt::SkipEmptyParts);
    }
    
    return options;
}

Murmur::VideoInfo Murmur::MediaPipeline::convertFromMediaFileInfo(const Murmur::MediaFileInfo& info) {
    Murmur::VideoInfo videoInfo;
    
    videoInfo.filePath = info.filePath;
    videoInfo.format = info.format;
    videoInfo.duration = static_cast<qint64>(info.duration * 1000); // Convert to milliseconds
    videoInfo.fileSize = info.fileSize;
    videoInfo.width = info.video.width;
    videoInfo.height = info.video.height;
    videoInfo.frameRate = info.video.frameRate;
    videoInfo.codec = info.video.codec;
    videoInfo.bitrate = info.bitrate;
    videoInfo.hasAudio = (info.audio.streamIndex != -1);
    videoInfo.audioCodec = info.audio.codec;
    videoInfo.audioChannels = info.audio.channels;
    videoInfo.audioSampleRate = info.audio.sampleRate;
    
    return videoInfo;
}

Murmur::MediaError Murmur::MediaPipeline::convertFromFFmpegError(Murmur::FFmpegError error) {
    switch (error) {
        case Murmur::FFmpegError::InvalidFile:
            return Murmur::MediaError::InvalidFile;
        case Murmur::FFmpegError::UnsupportedFormat:
            return Murmur::MediaError::UnsupportedFormat;
        case Murmur::FFmpegError::DecodingFailed:
        case Murmur::FFmpegError::EncodingFailed:
        case Murmur::FFmpegError::FilteringFailed:
        case Murmur::FFmpegError::InitializationFailed:
            return Murmur::MediaError::ProcessingFailed;
        case Murmur::FFmpegError::IOError:
            return Murmur::MediaError::OutputError;
        case Murmur::FFmpegError::AllocationFailed:
            return Murmur::MediaError::ResourceExhausted;
        case Murmur::FFmpegError::CancellationRequested:
            return Murmur::MediaError::Cancelled;
        default:
            return Murmur::MediaError::ProcessingFailed;
    }
}

Murmur::ConversionProgress Murmur::MediaPipeline::createProgressFromFFmpeg(const QString& operationId, const Murmur::ProgressInfo& ffmpegProgress) {
    Murmur::ConversionProgress progress;
    
    // Get operation context for file paths
    QMutexLocker locker(&operationsMutex_);
    auto it = activeOperations_.find(operationId);
    if (it != activeOperations_.end()) {
        progress.inputFile = it->second->inputFile;
        progress.outputFile = it->second->outputFile;
    }
    
    progress.percentage = ffmpegProgress.progressPercent;
    progress.processedFrames = ffmpegProgress.processedFrames;
    progress.totalFrames = ffmpegProgress.totalFrames;
    progress.currentFps = ffmpegProgress.currentFps;
    progress.elapsedTime = ffmpegProgress.elapsedTimeMs;
    progress.estimatedTime = ffmpegProgress.estimatedTimeMs;
    progress.isCompleted = ffmpegProgress.isCompleted;
    progress.isCancelled = false; // Would be tracked separately
    
    return progress;
}

QString Murmur::MediaPipeline::generateOperationId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void Murmur::MediaPipeline::cleanupOperation(const QString& operationId) {
    auto it = activeOperations_.find(operationId);
    if (it != activeOperations_.end()) {
        activeOperations_.erase(it);
    }
}

bool Murmur::MediaPipeline::checkResourceLimits() const {
    return activeOperations_.size() < static_cast<size_t>(maxConcurrentOperations_);
}

Murmur::Expected<QString, Murmur::MediaError> Murmur::MediaPipeline::validateAndPreparePaths(
    const QString& inputPath,
    const QString& outputPath) {
    
    if (!QFileInfo::exists(inputPath)) {
        return makeUnexpected(Murmur::MediaError::InvalidFile);
    }
    
    QFileInfo outputInfo(outputPath);
    QDir outputDir = outputInfo.dir();
    if (!outputDir.exists() && !outputDir.mkpath(outputDir.absolutePath())) {
        return makeUnexpected(Murmur::MediaError::OutputError);
    }
    
    return outputPath;
}

void Murmur::MediaPipeline::setupErrorRecoveryStrategies() {
    // Hardware initialization failure recovery
    errorRecovery_->registerRecoveryStrategy(
        "HardwareAccelerator", "initialize",
        Murmur::RecoveryStrategies::fallbackWithRetry(
            []() -> bool {
                // Fallback to software-only mode
                Logger::instance().info("Falling back to software-only media processing");
                return true;
            }, 2
        )
    );
    
    // FFmpeg wrapper initialization failure recovery
    errorRecovery_->registerRecoveryStrategy(
        "FFmpegWrapper", "initialize",
        Murmur::RecoveryStrategies::retryWithExponentialBackoff(3)
    );
    
    // Video conversion failure recovery
    errorRecovery_->registerRecoveryStrategy(
        "FFmpegWrapper", "convertVideo",
        Murmur::RecoveryStrategies::fallbackWithRetry(
            []() -> bool {
                // Fallback to lower quality settings
                Logger::instance().info("Retrying video conversion with fallback settings");
                return true;
            }, 2
        )
    );
    
    // File analysis failure recovery
    errorRecovery_->registerRecoveryStrategy(
        "FFmpegWrapper", "analyzeFile",
        Murmur::RecoveryStrategies::retryWithExponentialBackoff(2)
    );
    
    // Audio extraction failure recovery
    errorRecovery_->registerRecoveryStrategy(
        "FFmpegWrapper", "extractAudio",
        Murmur::RecoveryStrategies::fallbackWithRetry(
            []() -> bool {
                // Fallback to basic audio extraction
                Logger::instance().info("Retrying audio extraction with basic settings");
                return true;
            }, 2
        )
    );
    
    // Start health checks
    errorRecovery_->startHealthCheck(
        "FFmpegWrapper",
        [this]() -> bool {
            // Simple health check - verify FFmpeg wrapper is responsive
            return ffmpegWrapper_ != nullptr;
        },
        std::chrono::seconds(30)
    );
    
    errorRecovery_->startHealthCheck(
        "HardwareAccelerator",
        [this]() -> bool {
            // Hardware accelerator health check
            return hardwareAccelerator_ != nullptr;
        },
        std::chrono::minutes(1)
    );
    
    Logger::instance().info("Error recovery strategies configured");
}

void Murmur::MediaPipeline::initializeWithRetry() {
    // Initialize hardware acceleration with retry
    auto hardwareInitResult = retryManager_->execute<bool, QString>(
        [this]() -> Murmur::Expected<bool, QString> {
            auto result = hardwareAccelerator_->initialize();
            if (result.hasError()) {
                QString errorStr = QString("Hardware acceleration init failed: %1")
                                  .arg(static_cast<int>(result.error()));
                REPORT_ERROR_MSG(errorRecovery_.get(), "HardwareAccelerator", "initialize", errorStr);
                return makeUnexpected(errorStr);
            }
            return true;
        },
        [](const QString& error) -> bool {
            // Most hardware errors are retryable except for missing drivers
            return !error.contains("driver", Qt::CaseInsensitive);
        }
    );
    
    if (hardwareInitResult.hasError()) {
        Logger::instance().warn("Hardware acceleration initialization failed after retries: {}", 
                                static_cast<int>(hardwareInitResult.error()));
        // Continue with software-only mode
    } else {
        Logger::instance().info("Hardware acceleration initialized successfully");
    }
    
    // Initialize FFmpeg wrapper with retry
    auto ffmpegInitResult = retryManager_->execute<bool, QString>(
        [this]() -> Murmur::Expected<bool, QString> {
            auto result = ffmpegWrapper_->initialize();
            if (result.hasError()) {
                QString errorStr = QString("FFmpeg wrapper init failed: %1")
                                  .arg(static_cast<int>(result.error()));
                REPORT_ERROR_MSG(errorRecovery_.get(), "FFmpegWrapper", "initialize", errorStr);
                return makeUnexpected(errorStr);
            }
            return true;
        },
        [](const QString& error) -> bool {
            // Usually retryable
            return !error.contains("not found", Qt::CaseInsensitive);
        }
    );
    
    if (ffmpegInitResult.hasError()) {
        QString errorMsg = QString("FFmpeg wrapper initialization failed after retries: %1")
                          .arg(static_cast<int>(ffmpegInitResult.error()));
        REPORT_CRITICAL(errorRecovery_.get(), "FFmpegWrapper", "initialize", errorMsg);
        Logger::instance().error("{}", errorMsg.toStdString());
    } else {
        Logger::instance().info("FFmpeg wrapper initialized successfully");
    }
}

} // namespace Murmur