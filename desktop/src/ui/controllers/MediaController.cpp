#include "MediaController.hpp"
#include "../../core/common/Logger.hpp"
#include <QtConcurrent>
#include <QFileInfo>
#include <QUuid>
#include <QDateTime>
#include <QFutureWatcher>

namespace Murmur {

void MediaController::setReady(bool ready) {
    if (ready_ != ready) {
        ready_ = ready;
        Logger::instance().debug("MediaController ready state changed to: {}", ready ? "true" : "false");
        emit readyChanged();
    }
}

bool MediaController::isReady() const {
    return ready_;
}

void MediaController::updateReadyState() {
    bool isReadyNow = mediaPipeline_ != nullptr &&
                     videoPlayer_ != nullptr &&
                     storageManager_ != nullptr;
    setReady(isReadyNow);
}

MediaController::MediaController(QObject* parent)
    : QObject(parent), ready_(false) {
    Logger::instance().info("MediaController created");
}

void MediaController::setMediaPipeline(MediaPipeline* pipeline) {
    bool wasReady = isReady();
    Logger::instance().info("Setting MediaPipeline: {}", pipeline ? "valid" : "null");
    if (mediaPipeline_ != pipeline) {
        if (mediaPipeline_) {
            Logger::instance().info("Disconnecting old MediaPipeline");
            disconnect(mediaPipeline_, nullptr, this, nullptr);
        }
        
        mediaPipeline_ = pipeline;
        
        if (mediaPipeline_) {
            Logger::instance().info("Connecting MediaPipeline signals");
            connectPipelineSignals();
        }
        
        updateReadyState();
    }
    Logger::instance().info("MediaPipeline set: {}", mediaPipeline_ ? "valid" : "null");
}

void MediaController::setVideoPlayer(VideoPlayer* player) {
    bool wasReady = isReady();
    Logger::instance().info("Setting VideoPlayer: {}", player ? "valid" : "null");
    if (videoPlayer_ != player) {
        videoPlayer_ = player;
        
        updateReadyState();
    }
    Logger::instance().info("VideoPlayer set: {}", videoPlayer_ ? "valid" : "null");
}

void MediaController::setStorageManager(StorageManager* storage) {
    bool wasReady = isReady();
    Logger::instance().info("Setting StorageManager: {}", storage ? "valid" : "null");
    if (storageManager_ != storage) {
        storageManager_ = storage;
        
        updateReadyState();
    }
    Logger::instance().info("StorageManager set: {}", storageManager_ ? "valid" : "null");
}

void MediaController::loadTorrent(const QString& infoHash) {
    Logger::instance().info("Loading torrent for playback: {}", infoHash.toStdString());
    
    if (!storageManager_) {
        Logger::instance().error("StorageManager not available");
        return;
    }
    
    // Get media files associated with this torrent
    auto future = QtConcurrent::run([this, infoHash]() {
        auto mediaResult = storageManager_->getMediaByTorrent(infoHash);
        if (mediaResult.hasValue() && !mediaResult.value().isEmpty()) {
            MediaRecord media = mediaResult.value().first();
            QUrl fileUrl = QUrl::fromLocalFile(media.filePath);
            
            // Update on main thread
            QMetaObject::invokeMethod(this, [this, fileUrl]() {
                updateVideoSource(fileUrl);
            }, Qt::QueuedConnection);
        } else {
            Logger::instance().warn("No media found for torrent: {}", infoHash.toStdString());
        }
    });
}

void MediaController::loadLocalFile(const QUrl& filePath) {
    Logger::instance().info("Loading local file: {}", filePath.toString().toStdString());
    
    // Set current media file
    QString localPath = filePath.toLocalFile();
    if (localPath.isEmpty()) {
        // Handle file:// URLs that might not convert properly
        localPath = filePath.toString();
        if (localPath.startsWith("file://")) {
            localPath = localPath.mid(7); // Remove "file://" prefix
        }
    }
    
    Logger::instance().info("Local path: {}", localPath.toStdString());
    if (currentMediaFile_ != localPath) {
        currentMediaFile_ = localPath;
        emit currentMediaFileChanged();
    }
    
    // Set video source for playback
    updateVideoSource(filePath);
    
    if (videoPlayer_) {
        videoPlayer_->setSource(filePath);
        Logger::instance().info("Video source set in player");
    } else {
        Logger::instance().warn("VideoPlayer not available");
        emit errorOccurred("Video player not available");
        return;
    }
    
    Logger::instance().info("MediaPipeline available: {}", mediaPipeline_ ? "yes" : "no");
    if (!mediaPipeline_) {
        Logger::instance().error("MediaPipeline not available for analysis");
        // Still update the video source even if analysis fails
        updateVideoSource(filePath);
        return;
    }
    
    // Analyze the video file first
    Logger::instance().info("Starting video analysis with MediaPipeline");
    auto analyzeResult = mediaPipeline_->analyzeVideo(localPath);
    
    auto watcher = new QFutureWatcher<Expected<VideoInfo, MediaError>>(this);
    connect(watcher, &QFutureWatcher<Expected<VideoInfo, MediaError>>::finished, [this, filePath, localPath, watcher]() {
        Logger::instance().info("Video analysis finished");
        auto result = watcher->result();
        watcher->deleteLater();
        
        if (result.hasValue()) {
            VideoInfo info = result.value();
            Logger::instance().info("Video analysis successful");
            emit videoAnalyzed(localPath, info);
            
            // Store in database if storage manager is available
            if (storageManager_) {
                MediaRecord media;
                media.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                media.filePath = localPath;
                media.originalName = QFileInfo(localPath).fileName();
                media.fileSize = info.fileSize;
                media.duration = info.duration;
                media.width = info.width;
                media.height = info.height;
                media.frameRate = info.frameRate;
                media.videoCodec = info.codec;
                media.audioCodec = info.audioCodec;
                media.dateAdded = QDateTime::currentDateTime();
                
                storageManager_->addMedia(media);
            }
            
            // Auto-generate thumbnail
            QFileInfo fileInfo(localPath);
            QString baseName = fileInfo.baseName();
            QString outputDir = fileInfo.absolutePath();
            QString thumbnailPath = QString("%1/%2_thumbnail.jpg").arg(outputDir, baseName);
            
            if (!QFileInfo::exists(thumbnailPath)) {
                Logger::instance().info("Auto-generating thumbnail for: {}", localPath.toStdString());
                generateThumbnail(localPath, thumbnailPath, 10);
            }
        } else {
            Logger::instance().error("Failed to analyze video: {}", static_cast<int>(result.error()));
            emit errorOccurred("Failed to analyze video: " + QString::number(static_cast<int>(result.error())));
        }
        
        updateVideoSource(filePath);
    });
    
    watcher->setFuture(analyzeResult);
}

void MediaController::savePosition(qreal position) {
    if (playbackPosition_ != position) {
        playbackPosition_ = position;
        emit positionChanged();
        
        // Persist position to storage if available
        if (storageManager_ && !currentVideoSource_.isEmpty()) {
            QString filePath = currentVideoSource_.toLocalFile();
            
            // Find media record and update playback position
            auto future = QtConcurrent::run([this, filePath, position]() {
                auto mediaResult = storageManager_->searchMedia(QFileInfo(filePath).fileName());
                if (mediaResult.hasValue() && !mediaResult.value().isEmpty()) {
                    QString mediaId = mediaResult.value().first().id;
                    qint64 positionMs = static_cast<qint64>(position * 1000);
                    storageManager_->updatePlaybackPosition(mediaId, positionMs);
                }
            });
        }
    }
}

void MediaController::convertVideo(const QString& inputPath, const QString& outputPath, const QString& format) {
    Logger::instance().info("Converting video: {} to {}", inputPath.toStdString(), format.toStdString());
    
    if (!mediaPipeline_) {
        Logger::instance().error("MediaPipeline not available");
        emit conversionError("", "Media pipeline not available");
        return;
    }
    
    setProcessing(true);
    
    ConversionSettings settings;
    settings.outputFormat = format;
    
    // Configure settings based on format
    if (format == "mp4") {
        settings.videoCodec = "libx264";
        settings.audioCodec = "aac";
    } else if (format == "mov") {
        settings.videoCodec = "libx264";
        settings.audioCodec = "aac";
        // Ensure proper audio sync for MOV
        settings.customOptions = "-async 1 -vsync 1";
    } else if (format == "webm") {
        settings.videoCodec = "libvpx-vp9";
        settings.audioCodec = "libopus";
    } else if (format == "avi") {
        settings.videoCodec = "libx264";
        settings.audioCodec = "mp3";
    } else if (format == "mkv") {
        settings.videoCodec = "libx264";  // More compatible for MKV
        settings.audioCodec = "aac"; // More compatible for MKV
        settings.outputFormat = "matroska"; // Explicitly set container format for MKV
        settings.customOptions = "-preset fast -crf 23";
    } else {
        // Default fallback
        settings.videoCodec = "libx264";
        settings.audioCodec = "aac";
    }
    
    auto conversionResult = mediaPipeline_->convertVideo(inputPath, outputPath, settings);
    
    auto watcher = new QFutureWatcher<Expected<QString, MediaError>>(this);
    connect(watcher, &QFutureWatcher<Expected<QString, MediaError>>::finished, [this, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        setProcessing(false);
        
        if (result.hasValue()) {
            emit conversionCompleted("", result.value());
        } else {
            emit conversionError("", "Conversion failed: " + QString::number(static_cast<int>(result.error())));
        }
    });
    
    watcher->setFuture(conversionResult);
}

void MediaController::extractAudio(const QString& videoPath, const QString& outputPath) {
    Logger::instance().info("Extracting audio from: {}", videoPath.toStdString());
    
    if (!mediaPipeline_) {
        Logger::instance().error("MediaPipeline not available");
        emit conversionError("", "Media pipeline not available");
        return;
    }
    
    setProcessing(true);
    
    auto extractionResult = mediaPipeline_->extractAudio(videoPath, outputPath, "wav");
    
    auto watcher = new QFutureWatcher<Expected<QString, MediaError>>(this);
    connect(watcher, &QFutureWatcher<Expected<QString, MediaError>>::finished, [this, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        setProcessing(false);
        
        if (result.hasValue()) {
            emit conversionCompleted("", result.value());
        } else {
            emit conversionError("", "Audio extraction failed: " + QString::number(static_cast<int>(result.error())));
        }
    });
    
    watcher->setFuture(extractionResult);
}

void MediaController::generateThumbnail(const QString& videoPath, const QString& outputPath, int timeOffset) {
    Logger::instance().info("Generating thumbnail for: {}", videoPath.toStdString());
    
    if (!mediaPipeline_) {
        Logger::instance().error("MediaPipeline not available");
        return;
    }
    
    auto thumbnailResult = mediaPipeline_->generateThumbnail(videoPath, outputPath, timeOffset);
    
    auto watcher = new QFutureWatcher<Expected<QString, MediaError>>(this);
    connect(watcher, &QFutureWatcher<Expected<QString, MediaError>>::finished, [this, videoPath, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        if (result.hasValue()) {
            emit thumbnailGenerated(videoPath, result.value());
        } else {
            Logger::instance().error("Thumbnail generation failed: {}", static_cast<int>(result.error()));
        }
    });
    
    watcher->setFuture(thumbnailResult);
}

void MediaController::cancelOperation(const QString& operationId) {
    Logger::instance().info("Cancelling operation: {}", operationId.toStdString());
    
    if (mediaPipeline_) {
        mediaPipeline_->cancelOperation(operationId);
    }
    
    setProcessing(false);
}

void MediaController::cancelOperation() {
    if (!currentOperationId_.isEmpty()) {
        cancelOperation(currentOperationId_);
        emit operationCancelled(currentOperationId_);
        currentOperationId_.clear();
    }
}

void MediaController::cancelAllOperations() {
    Logger::instance().info("Cancelling all operations");
    
    if (mediaPipeline_) {
        mediaPipeline_->cancelAllOperations();
    }
    
    for (auto it = activeOperations_.begin(); it != activeOperations_.end(); ++it) {
        emit operationCancelled(it.key());
    }
    activeOperations_.clear();
    currentOperationId_.clear();
    
    setProcessing(false);
}

void MediaController::convertVideo(const QString& format) {
    Logger::instance().info("Converting video to format: {}", format.toStdString());
    
    if (currentMediaFile_.isEmpty()) {
        Logger::instance().error("No media file loaded for conversion");
        emit conversionError("", "No video loaded. Please load a video file first.");
        return;
    }
    
    if (!mediaPipeline_) {
        Logger::instance().error("MediaPipeline not available for conversion");
        emit conversionError("", "Media pipeline not available");
        return;
    }
    
    // Generate output filename
    QFileInfo fileInfo(currentMediaFile_);
    QString baseName = fileInfo.baseName();
    QString outputDir = fileInfo.absolutePath();
    QString extension = format.startsWith(".") ? format.mid(1) : format;
    QString outputPath = QString("%1/%2_converted.%3").arg(outputDir, baseName, extension);
    
    Logger::instance().info("Output path: {}", outputPath.toStdString());
    
    // Use the existing convertVideo method
    convertVideo(currentMediaFile_, outputPath, extension);
}

void MediaController::generateThumbnailForCurrentVideo() {
    Logger::instance().info("Generating thumbnail for current video");
    
    if (currentMediaFile_.isEmpty()) {
        Logger::instance().error("No media file loaded for thumbnail generation");
        emit errorOccurred("No video loaded. Please load a video file first.");
        return;
    }
    
    if (!mediaPipeline_) {
        Logger::instance().error("MediaPipeline not available for thumbnail generation");
        emit errorOccurred("Media pipeline not available");
        return;
    }
    
    // Generate thumbnail filename
    QFileInfo fileInfo(currentMediaFile_);
    QString baseName = fileInfo.baseName();
    QString outputDir = fileInfo.absolutePath();
    QString thumbnailPath = QString("%1/%2_thumbnail.jpg").arg(outputDir, baseName);
    
    Logger::instance().info("Thumbnail path: {}", thumbnailPath.toStdString());
    
    // Generate thumbnail at 10% of video duration
    generateThumbnail(currentMediaFile_, thumbnailPath, 10);
}

void MediaController::startConversion(const QString& outputPath, const QVariantMap& settings) {
    if (currentMediaFile_.isEmpty()) {
        emit errorOccurred("No media file loaded");
        return;
    }
    
    // Store output path and settings
    if (outputPath_ != outputPath) {
        outputPath_ = outputPath;
        emit outputPathChanged();
    }
    
    if (!settings.isEmpty()) {
        conversionSettings_ = settings;
    }
    
    // Generate operation ID
    currentOperationId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Start conversion using existing convertVideo method
    QString format = settings.value("outputFormat", "mp4").toString();
    convertVideo(currentMediaFile_, outputPath, format);
}

void MediaController::setConversionSettings(const QVariantMap& settings) {
    conversionSettings_ = settings;
}

QString MediaController::getCurrentMediaFile() const {
    return currentMediaFile_;
}

QString MediaController::getOutputPath() const {
    return outputPath_;
}

QStringList MediaController::getActiveOperations() const {
    return activeOperations_.keys();
}

void MediaController::onConversionProgress(const QString& operationId, const ConversionProgress& progress) {
    emit conversionProgress(operationId, progress.percentage / 100.0);
    
    // Emit UI-friendly progress signal
    QVariantMap progressData;
    progressData["progress"] = progress.percentage;
    progressData["operationId"] = operationId;
    progressData["status"] = "Converting";
    emit progressUpdated(progressData);
}

void MediaController::onConversionCompleted(const QString& operationId, const QString& outputPath) {
    setProcessing(false);
    emit conversionCompleted(operationId, outputPath);
    emit operationCompleted("Conversion completed successfully");
    
    // Clear current operation if it matches
    if (currentOperationId_ == operationId) {
        currentOperationId_.clear();
    }
}

void MediaController::onConversionFailed(const QString& operationId, MediaError error, const QString& errorString) {
    setProcessing(false);
    QString errorMessage = QString("Conversion failed (error %1): %2")
                          .arg(static_cast<int>(error))
                          .arg(errorString);
    emit conversionError(operationId, errorMessage);
    emit errorOccurred(errorMessage);
    
    // Clear current operation if it matches
    if (currentOperationId_ == operationId) {
        currentOperationId_.clear();
    }
}

void MediaController::setProcessing(bool processing) {
    if (isProcessing_ != processing) {
        isProcessing_ = processing;
        emit processingChanged();
    }
}

void MediaController::updateVideoSource(const QUrl& source) {
    if (currentVideoSource_ != source) {
        currentVideoSource_ = source;
        emit sourceChanged();
    }
}

void MediaController::connectPipelineSignals() {
    Logger::instance().info("Connecting MediaPipeline signals");
    if (!mediaPipeline_) {
        Logger::instance().warn("MediaPipeline is null, cannot connect signals");
        return;
    }
    
    connect(mediaPipeline_, &MediaPipeline::conversionProgress,
            this, &MediaController::onConversionProgress);
    connect(mediaPipeline_, &MediaPipeline::conversionCompleted,
            this, &MediaController::onConversionCompleted);
    connect(mediaPipeline_, &MediaPipeline::conversionFailed,
            this, &MediaController::onConversionFailed);
    Logger::instance().info("MediaPipeline signals connected");
}

} // namespace Murmur