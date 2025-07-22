#include "VideoPlayer.hpp"
#include "../common/Logger.hpp"
#include "../security/InputValidator.hpp"

#include <QMediaFormat>
#include <QMediaMetaData>
#include <QFileInfo>
#include <QDir>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QDateTime>
#include <QUrlQuery>
#include <QTimer>
#include <QtConcurrent>
#include <QThreadPool>
#include <QVideoFrame>

namespace Murmur {

// Static member initialization
const QStringList VideoPlayer::SUPPORTED_VIDEO_FORMATS = {
    "mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "3gp", "ogv", "ts", "m2ts"
};

const QStringList VideoPlayer::SUPPORTED_AUDIO_FORMATS = {
    "mp3", "wav", "flac", "aac", "ogg", "m4a", "wma", "opus"
};

const QStringList VideoPlayer::SUPPORTED_SUBTITLE_FORMATS = {
    "srt", "vtt", "ass", "ssa", "sub", "idx", "sup"
};

VideoPlayer::VideoPlayer(QObject* parent)
    : QObject(parent)
    , autoSaveTimer_(new QTimer(this)) {
    
    initializePlayer();
    
    // Setup auto-save timer
    autoSaveTimer_->setSingleShot(false);
    autoSaveTimer_->setInterval(DEFAULT_AUTO_SAVE_INTERVAL_MS);
    connect(autoSaveTimer_, &QTimer::timeout, this, &VideoPlayer::onAutoSaveTimer);
    
    // Initialize performance tracking
    performanceMetrics_.sessionStart = QDateTime::currentDateTime();
    
    Logger::instance().info("VideoPlayer initialized");
}

VideoPlayer::~VideoPlayer() {
    if (autoSaveEnabled_ && storageManager_ && !mediaId_.isEmpty()) {
        persistCurrentPosition();
    }
    
    // Finalize performance metrics before saving
    if (mediaPlayer_ && mediaPlayer_->playbackState() == QMediaPlayer::PlayingState) {
        if (performanceMetrics_.playbackStartTime.isValid()) {
            performanceMetrics_.totalPlaybackTime += performanceMetrics_.playbackStartTime.msecsTo(QDateTime::currentDateTime());
        }
    }
    savePerformanceMetrics();
    
    if (mediaPlayer_) {
        mediaPlayer_->stop();
    }
}

void VideoPlayer::initializePlayer() {
    // Create Qt Multimedia components
    mediaPlayer_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    videoSink_ = new QVideoSink(this);
    
    // Connect audio and video outputs
    mediaPlayer_->setAudioOutput(audioOutput_);
    mediaPlayer_->setVideoSink(videoSink_);
    
    // Connect signals
    connect(mediaPlayer_, &QMediaPlayer::playbackStateChanged,
            this, &VideoPlayer::onMediaPlayerStateChanged);
    connect(mediaPlayer_, &QMediaPlayer::mediaStatusChanged,
            this, &VideoPlayer::onMediaPlayerStatusChanged);
    connect(mediaPlayer_, &QMediaPlayer::positionChanged,
            this, &VideoPlayer::onMediaPlayerPositionChanged);
    connect(mediaPlayer_, &QMediaPlayer::durationChanged,
            this, &VideoPlayer::onMediaPlayerDurationChanged);
    connect(mediaPlayer_, &QMediaPlayer::errorOccurred,
            this, &VideoPlayer::onMediaPlayerError);
    connect(mediaPlayer_, &QMediaPlayer::bufferProgressChanged,
            this, &VideoPlayer::onMediaPlayerBufferProgressChanged);
    connect(mediaPlayer_, &QMediaPlayer::playbackRateChanged,
            this, &VideoPlayer::onMediaPlayerPlaybackRateChanged);
    connect(mediaPlayer_, &QMediaPlayer::tracksChanged,
            this, &VideoPlayer::onMediaPlayerTracksChanged);

    // Connect property change signals
    connect(mediaPlayer_, &QMediaPlayer::hasVideoChanged, this, &VideoPlayer::hasVideoChanged);
    connect(mediaPlayer_, &QMediaPlayer::hasAudioChanged, this, &VideoPlayer::hasAudioChanged);
    connect(mediaPlayer_, &QMediaPlayer::seekableChanged, this, &VideoPlayer::seekableChanged);
    connect(audioOutput_, &QAudioOutput::mutedChanged, this, &VideoPlayer::mutedChanged);
    connect(audioOutput_, &QAudioOutput::volumeChanged, this, [this](float vol) {
        emit volumeChanged(static_cast<int>(vol * 100.0f));
    });
    
    // Set default volume
    audioOutput_->setVolume(0.8f);
    
    // Enable hardware acceleration by default
    if (hardwareAccelerationEnabled_) {
        // Qt 6 automatically uses hardware acceleration when available
        Logger::instance().info("Hardware acceleration enabled");
    }
}

QUrl VideoPlayer::source() const {
    return currentSource_;
}

void VideoPlayer::setSource(const QUrl& source) {
    QMutexLocker locker(&stateMutex_);
    
    if (currentSource_ == source) {
        return;
    }
    
    // Save current position before changing source
    if (autoSaveEnabled_ && storageManager_ && !mediaId_.isEmpty()) {
        persistCurrentPosition();
    }
    
    // Validate source
    if (!source.isEmpty() && !isFormatSupported(source)) {
        handlePlaybackError(PlayerError::InvalidMediaFormat, 
                          "Unsupported media format: " + source.toString());
        return;
    }
    
    currentSource_ = source;
    mediaPlayer_->setSource(source);
    
    // Reset tracks and metadata
    audioTracks_.clear();
    subtitleTracks_.clear();
    currentAudioTrack_ = -1;
    currentSubtitleTrack_ = -1;
    currentMetadata_ = VideoMetadata{};
    
    Logger::instance().info("Source set to: {}", source.toString().toStdString());
    emit sourceChanged(source);
}

PlaybackState VideoPlayer::playbackState() const {
    return currentPlaybackState_;
}

MediaStatus VideoPlayer::mediaStatus() const {
    return currentMediaStatus_;
}

qint64 VideoPlayer::position() const {
    return mediaPlayer_->position();
}

void VideoPlayer::setPosition(qint64 position) {
    if (position < 0 || (duration() > 0 && position > duration())) {
        Logger::instance().warn("Invalid position: {}", position);
        return;
    }
    
    mediaPlayer_->setPosition(position);
    performanceMetrics_.totalSeeks++;
    
    Logger::instance().debug("Position set to: {}", position);
}

qint64 VideoPlayer::duration() const {
    return mediaPlayer_->duration();
}

double VideoPlayer::playbackRate() const {
    return mediaPlayer_->playbackRate();
}

void VideoPlayer::setPlaybackRate(double rate) {
    if (rate <= 0.0 || rate > 4.0) {  // Reasonable limits
        Logger::instance().warn("Invalid playback rate: {}", rate);
        return;
    }
    
    mediaPlayer_->setPlaybackRate(rate);
}

int VideoPlayer::volume() const {
    return static_cast<int>(audioOutput_->volume() * 100.0f);
}

void VideoPlayer::setVolume(int volume) {
    int clampedVolume = qBound(0, volume, 100);
    audioOutput_->setVolume(clampedVolume / 100.0f);
}

bool VideoPlayer::isMuted() const {
    return audioOutput_->isMuted();
}

void VideoPlayer::setMuted(bool muted) {
    audioOutput_->setMuted(muted);
}

bool VideoPlayer::hasVideo() const {
    return mediaPlayer_->hasVideo();
}

bool VideoPlayer::hasAudio() const {
    return mediaPlayer_->hasAudio();
}

bool VideoPlayer::isSeekable() const {
    return mediaPlayer_->isSeekable();
}

QVideoSink* VideoPlayer::videoSink() const {
    return videoSink_;
}

void VideoPlayer::setStorageManager(StorageManager* storage) {
    storageManager_ = storage;
}

void VideoPlayer::setMediaId(const QString& mediaId) {
    if (mediaId_ != mediaId) {
        // Save position for previous media
        if (autoSaveEnabled_ && storageManager_ && !mediaId_.isEmpty()) {
            persistCurrentPosition();
        }
        
        mediaId_ = mediaId;
        
        // Restore position for new media
        if (!mediaId_.isEmpty()) {
            auto positionResult = loadSavedPosition();
            if (positionResult.hasValue() && positionResult.value() > 0) {
                setPosition(positionResult.value());
                emit positionRestored(positionResult.value());
            }
        }
    }
}

QString VideoPlayer::mediaId() const {
    return mediaId_;
}

Expected<VideoMetadata, PlayerError> VideoPlayer::getMetadata() const {
    if (currentMediaStatus_ == MediaStatus::NoMedia) {
        return makeUnexpected(PlayerError::MediaLoadFailed);
    }
    
    return currentMetadata_;
}

QList<AudioTrack> VideoPlayer::getAudioTracks() const {
    return audioTracks_;
}

QList<SubtitleTrack> VideoPlayer::getSubtitleTracks() const {
    return subtitleTracks_;
}

void VideoPlayer::play() {
    if (currentSource_.isEmpty()) {
        handlePlaybackError(PlayerError::MediaLoadFailed, "No media source set");
        return;
    }
    
    mediaPlayer_->play();
    
    if (autoSaveEnabled_) {
        autoSaveTimer_->start();
    }
    
    Logger::instance().info("Playback started");
}

void VideoPlayer::pause() {
    mediaPlayer_->pause();
    
    if (autoSaveEnabled_) {
        autoSaveTimer_->stop();
        persistCurrentPosition();
    }
    
    Logger::instance().info("Playback paused");
}

void VideoPlayer::stop() {
    mediaPlayer_->stop();
    
    if (autoSaveEnabled_) {
        autoSaveTimer_->stop();
        persistCurrentPosition();
    }
    
    Logger::instance().info("Playback stopped");
}

void VideoPlayer::togglePlayPause() {
    if (currentPlaybackState_ == PlaybackState::Playing) {
        pause();
    } else {
        play();
    }
}

void VideoPlayer::seekForward(qint64 ms) {
    qint64 newPosition = position() + ms;
    setPosition(newPosition);
}

void VideoPlayer::seekBackward(qint64 ms) {
    qint64 newPosition = position() - ms;
    setPosition(qMax(0LL, newPosition));
}

void VideoPlayer::seekToPercentage(double percentage) {
    if (duration() > 0) {
        qint64 newPosition = static_cast<qint64>(duration() * qBound(0.0, percentage, 1.0));
        setPosition(newPosition);
    }
}

void VideoPlayer::stepForward() {
    // Step forward by one frame (assuming 25 fps as fallback)
    double frameRate = currentMetadata_.frameRate > 0 ? currentMetadata_.frameRate : 25.0;
    qint64 frameTime = static_cast<qint64>(1000.0 / frameRate);
    seekForward(frameTime);
}

void VideoPlayer::stepBackward() {
    double frameRate = currentMetadata_.frameRate > 0 ? currentMetadata_.frameRate : 25.0;
    qint64 frameTime = static_cast<qint64>(1000.0 / frameRate);
    seekBackward(frameTime);
}

void VideoPlayer::increaseSpeed() {
    double currentRate = playbackRate();
    double newRate = qMin(4.0, currentRate * 1.25);  // Increase by 25%, max 4x
    setPlaybackRate(newRate);
}

void VideoPlayer::decreaseSpeed() {
    double currentRate = playbackRate();
    double newRate = qMax(0.25, currentRate * 0.8);  // Decrease by 20%, min 0.25x
    setPlaybackRate(newRate);
}

void VideoPlayer::resetSpeed() {
    setPlaybackRate(1.0);
}

void VideoPlayer::increaseVolume(int delta) {
    setVolume(volume() + delta);
}

void VideoPlayer::decreaseVolume(int delta) {
    setVolume(volume() - delta);
}

void VideoPlayer::toggleMute() {
    setMuted(!isMuted());
}

Expected<bool, PlayerError> VideoPlayer::savePosition() {
    if (!storageManager_ || mediaId_.isEmpty()) {
        return makeUnexpected(PlayerError::ResourceNotAvailable);
    }
    
    persistCurrentPosition();
    return true;
}

Expected<bool, PlayerError> VideoPlayer::restorePosition() {
    if (!storageManager_ || mediaId_.isEmpty()) {
        return makeUnexpected(PlayerError::ResourceNotAvailable);
    }
    
    auto positionResult = loadSavedPosition();
    if (positionResult.hasError()) {
        return makeUnexpected(positionResult.error());
    }
    
    setPosition(positionResult.value());
    emit positionRestored(positionResult.value());
    return true;
}

void VideoPlayer::enableAutoSavePosition(bool enabled, int intervalMs) {
    autoSaveEnabled_ = enabled;
    
    if (enabled) {
        autoSaveTimer_->setInterval(intervalMs);
        if (currentPlaybackState_ == PlaybackState::Playing) {
            autoSaveTimer_->start();
        }
    } else {
        autoSaveTimer_->stop();
    }
}

void VideoPlayer::setHardwareAccelerationEnabled(bool enabled) {
    hardwareAccelerationEnabled_ = enabled;
    Logger::instance().info("Hardware acceleration {}", enabled ? "enabled" : "disabled");
}

bool VideoPlayer::isHardwareAccelerationEnabled() const {
    return hardwareAccelerationEnabled_;
}

bool VideoPlayer::isHardwareAccelerationSupported() const {
    // Qt 6 automatically detects and uses hardware acceleration when available
    return true;  // Assume supported, Qt will fallback to software if needed
}

void VideoPlayer::setBufferSize(qint64 sizeBytes) {
    bufferSize_ = qBound(1024LL * 1024, sizeBytes, MAX_BUFFER_SIZE_MB * 1024LL * 1024);
    // Qt MediaPlayer handles buffering automatically
}

void VideoPlayer::setNetworkCacheSize(qint64 sizeBytes) {
    networkCacheSize_ = qBound(1024LL * 1024, sizeBytes, MAX_CACHE_SIZE_MB * 1024LL * 1024);
    // Qt MediaPlayer handles network caching automatically
}

qint64 VideoPlayer::bufferedBytes() const {
    // Qt 6 MediaPlayer provides buffer progress, calculate approximate buffered bytes
    if (!mediaPlayer_ || duration() <= 0) {
        return 0;
    }
    
    // Estimate buffered bytes based on buffer progress and total file size
    // This is an approximation since Qt doesn't expose exact buffered bytes
    qint64 totalDuration = duration();
    qint64 currentPosition = position();
    
    if (currentMetadata_.bitrate > 0) {
        // Calculate based on bitrate
        qint64 totalBytes = (currentMetadata_.bitrate * totalDuration) / 8000; // Convert from bits/ms to bytes
        
        // Assume we have at least 5 seconds of buffer ahead
        qint64 bufferAheadMs = 5000; 
        qint64 estimatedBufferedBytes = (currentMetadata_.bitrate * bufferAheadMs) / 8000;
        
        return qMin(estimatedBufferedBytes, totalBytes - ((currentMetadata_.bitrate * currentPosition) / 8000));
    }
    
    // Fallback: estimate based on typical video bitrates
    qint64 estimatedBitrate = 2000000; // 2 Mbps default
    qint64 bufferAheadMs = 5000; // 5 seconds
    return (estimatedBitrate * bufferAheadMs) / 8000; // Convert to bytes
}

Expected<QString, PlayerError> VideoPlayer::captureSnapshot(const QString& outputPath) {
    if (!hasVideo() || !videoSink_) {
        return makeUnexpected(PlayerError::ResourceNotAvailable);
    }

    QVideoFrame frame = videoSink_->videoFrame();
    if (!frame.isValid()) {
        Logger::instance().error("VideoPlayer: Failed to capture frame, invalid video frame.");
        return makeUnexpected(PlayerError::ResourceNotAvailable);
    }

    QImage image = frame.toImage();
    if (image.isNull()) {
        Logger::instance().error("VideoPlayer: Failed to convert video frame to image.");
        return makeUnexpected(PlayerError::ResourceNotAvailable);
    }

    if (!image.save(outputPath)) {
        Logger::instance().error("VideoPlayer: Failed to save captured frame: {}", outputPath.toStdString());
        return makeUnexpected(PlayerError::ResourceNotAvailable);
    }
    
    Logger::instance().info("VideoPlayer: Snapshot saved: {}", outputPath.toStdString());
    emit snapshotCaptured(outputPath);
    return outputPath;
}

Expected<QList<QString>, PlayerError> VideoPlayer::generateThumbnails(
    const QString& outputDir, 
    int count,
    const QSize& size) {
    
    if (!hasVideo() || duration() <= 0 || !videoSink_) {
        return makeUnexpected(PlayerError::MediaLoadFailed);
    }
    
    QDir dir(outputDir);
    if (!dir.exists() && !dir.mkpath(".")) {
        Logger::instance().error("VideoPlayer: Failed to create thumbnail directory: {}", outputDir.toStdString());
        return makeUnexpected(PlayerError::ResourceNotAvailable);
    }

    // Save original state to restore later
    qint64 originalPosition = position();
    auto originalState = playbackState();
    if (originalState == PlaybackState::Playing) {
        pause();
    }
    
    QList<QString> thumbnails;
    qint64 interval = duration() / (count + 1);
    
    for (int i = 0; i < count; ++i) {
        qint64 thumbnailPosition = (i + 1) * interval;
        
        QImage frameImage;
        QEventLoop waitLoop;
        
        auto c = connect(videoSink_, &QVideoSink::videoFrameChanged, [&](const QVideoFrame &frame) {
            // Wait for a frame at or after the desired position
            if (mediaPlayer_->position() >= thumbnailPosition - 50) { // -50ms tolerance
                frameImage = frame.toImage();
                waitLoop.quit();
            }
        });

        mediaPlayer_->setPosition(thumbnailPosition);
        
        QTimer::singleShot(2000, &waitLoop, &QEventLoop::quit); // 2-second timeout for seek+capture
        waitLoop.exec();
        disconnect(c);
        
        if (frameImage.isNull()) {
            Logger::instance().warn("VideoPlayer: Timed out or failed to get frame for thumbnail at {}", thumbnailPosition);
            continue; // Try next one
        }
        
        QImage thumbnail = frameImage.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QString thumbnailPath = QString("%1/thumbnail_%2.jpg").arg(outputDir).arg(i + 1, 3, 10, QChar('0'));
        
        if (thumbnail.save(thumbnailPath)) {
            thumbnails.append(thumbnailPath);
            Logger::instance().info("VideoPlayer: Generated thumbnail {}/{}: {}", 
                                   i + 1, count, thumbnailPath.toStdString());
        } else {
            Logger::instance().error("VideoPlayer: Failed to save thumbnail: {}", thumbnailPath.toStdString());
        }
    }
    
    // Restore original position and state
    setPosition(originalPosition);
    if (originalState == PlaybackState::Playing) {
        play();
    }
    
    if (thumbnails.isEmpty()) {
        Logger::instance().error("VideoPlayer: No thumbnails were generated");
        return makeUnexpected(PlayerError::ResourceNotAvailable);
    }
    
    emit thumbnailsGenerated(thumbnails);
    return thumbnails;
}

// Private slot implementations
void VideoPlayer::onMediaPlayerStateChanged(QMediaPlayer::PlaybackState state) {
    PlaybackState oldState = currentPlaybackState_;
    PlaybackState newState = convertPlaybackState(state);

    if (oldState == PlaybackState::Playing && newState != PlaybackState::Playing) {
        if (performanceMetrics_.playbackStartTime.isValid()) {
            performanceMetrics_.totalPlaybackTime += performanceMetrics_.playbackStartTime.msecsTo(QDateTime::currentDateTime());
            performanceMetrics_.playbackStartTime = QDateTime(); // Invalidate
        }
    } else if (oldState != PlaybackState::Playing && newState == PlaybackState::Playing) {
        performanceMetrics_.playbackStartTime = QDateTime::currentDateTime();
    }

    if (currentPlaybackState_ != newState) {
        currentPlaybackState_ = newState;
        emit playbackStateChanged(newState);
        updatePerformanceMetrics();
    }
}

void VideoPlayer::onMediaPlayerStatusChanged(QMediaPlayer::MediaStatus status) {
    MediaStatus newStatus = convertMediaStatus(status);
    if (currentMediaStatus_ != newStatus) {
        currentMediaStatus_ = newStatus;
        emit mediaStatusChanged(newStatus);
        
        if (newStatus == MediaStatus::Loaded) {
            updateMetadata();
            detectTracks();
            
            // Restore position if we have a saved one
            if (!mediaId_.isEmpty()) {
                auto positionResult = loadSavedPosition();
                if (positionResult.hasValue() && positionResult.value() > 0) {
                    setPosition(positionResult.value());
                }
            }
        }
    }
}

void VideoPlayer::onMediaPlayerPositionChanged(qint64 position) {
    emit positionChanged(position);
}

void VideoPlayer::onMediaPlayerDurationChanged(qint64 duration) {
    emit durationChanged(duration);
}

void VideoPlayer::onMediaPlayerError(QMediaPlayer::Error error, const QString& errorString) {
    PlayerError playerError = mapMediaPlayerError(error);
    handlePlaybackError(playerError, errorString);
    performanceMetrics_.errorCount++;
}

void VideoPlayer::onMediaPlayerBufferProgressChanged(float progress) {
    emit bufferingProgressChanged(static_cast<double>(progress));
    
    if (progress < 1.0f) {
        performanceMetrics_.bufferingEvents++;
    }
}

void VideoPlayer::onMediaPlayerPlaybackRateChanged(qreal rate) {
    emit playbackRateChanged(static_cast<double>(rate));
}

void VideoPlayer::onMediaPlayerTracksChanged() {
    detectTracks();
}

void VideoPlayer::onAutoSaveTimer() {
    if (currentPlaybackState_ == PlaybackState::Playing) {
        persistCurrentPosition();
    }
}

// Private helper methods
void VideoPlayer::updateMetadata() {
    currentMetadata_ = VideoMetadata{};
    
    QMediaMetaData metaData = mediaPlayer_->metaData();
    
    currentMetadata_.title = metaData.value(QMediaMetaData::Title).toString();
    currentMetadata_.description = metaData.value(QMediaMetaData::Comment).toString();
    currentMetadata_.duration = duration();
    
    // Video properties
    QSize resolution = metaData.value(QMediaMetaData::Resolution).toSize();
    currentMetadata_.width = resolution.width();
    currentMetadata_.height = resolution.height();
    currentMetadata_.frameRate = metaData.value(QMediaMetaData::VideoFrameRate).toDouble();
    currentMetadata_.videoCodec = metaData.value(QMediaMetaData::VideoCodec).toString();
    currentMetadata_.audioCodec = metaData.value(QMediaMetaData::AudioCodec).toString();
    currentMetadata_.bitrate = metaData.value(QMediaMetaData::VideoBitRate).toLongLong();
    
    emit metadataChanged(currentMetadata_);
}

void VideoPlayer::detectTracks() {
    audioTracks_.clear();
    subtitleTracks_.clear();

    if (!mediaPlayer_) return;

    // Audio Tracks
    const auto& qAudioTracks = mediaPlayer_->audioTracks();
    for (int i = 0; i < qAudioTracks.size(); ++i) {
        const auto& trackMeta = qAudioTracks[i];
        AudioTrack newTrack;
        newTrack.id = i;
        newTrack.language = trackMeta.value(QMediaMetaData::Language).toString();
        newTrack.title = trackMeta.value(QMediaMetaData::Title).toString();
        if (newTrack.title.isEmpty()) {
            newTrack.title = trackMeta.value(QMediaMetaData::Comment).toString();
        }
        if (newTrack.title.isEmpty()) {
            newTrack.title = QString("Track %1 (%2)").arg(i + 1).arg(newTrack.language);
        }
        newTrack.codec = trackMeta.value(QMediaMetaData::AudioCodec).toString();
        newTrack.channels = 0; // Not available via high-level QMediaMetaData
        newTrack.sampleRate = 0; // Not available via high-level QMediaMetaData
        newTrack.isDefault = false; // Not available
        audioTracks_.append(newTrack);
    }

    // Subtitle Tracks
    const auto& qSubtitleTracks = mediaPlayer_->subtitleTracks();
    for (int i = 0; i < qSubtitleTracks.size(); ++i) {
        const auto& trackMeta = qSubtitleTracks[i];
        SubtitleTrack newTrack;
        newTrack.id = i;
        newTrack.language = trackMeta.value(QMediaMetaData::Language).toString();
        newTrack.title = trackMeta.value(QMediaMetaData::Title).toString();
        if (newTrack.title.isEmpty()) {
             newTrack.title = QString("Subtitle %1 (%2)").arg(i + 1).arg(newTrack.language);
        }
        newTrack.codec = trackMeta.value(QMediaMetaData::FileFormat).toString(); // Best guess for subtitle format
        newTrack.isDefault = false; // Not available
        newTrack.isForced = false;  // Not available
        subtitleTracks_.append(newTrack);
    }

    emit audioTracksChanged(audioTracks_);
    emit subtitleTracksChanged(subtitleTracks_);
}

void VideoPlayer::persistCurrentPosition() {
    if (!storageManager_ || mediaId_.isEmpty()) {
        return;
    }
    
    qint64 currentPos = position();
    if (currentPos > 0) {
        // Use QThreadPool::start for fire-and-forget task
        QThreadPool::globalInstance()->start([this, currentPos]() {
            auto result = storageManager_->updatePlaybackPosition(mediaId_, currentPos);
            if (result.hasValue()) {
                QMetaObject::invokeMethod(this, [this, currentPos]() {
                    emit positionSaved(currentPos);
                }, Qt::QueuedConnection);
            }
        });
    }
}

Expected<qint64, PlayerError> VideoPlayer::loadSavedPosition() {
    if (!storageManager_ || mediaId_.isEmpty()) {
        return makeUnexpected(PlayerError::ResourceNotAvailable);
    }
    
    auto mediaResult = storageManager_->getMedia(mediaId_);
    if (mediaResult.hasError()) {
        return makeUnexpected(PlayerError::MediaLoadFailed);
    }
    
    MediaRecord media = mediaResult.value();
    return media.playbackPosition;
}

bool VideoPlayer::isFormatSupported(const QUrl& source) const {
    if (source.isEmpty()) {
        return false;
    }
    
    QString fileName = source.fileName();
    QFileInfo fileInfo(fileName);
    QString extension = fileInfo.suffix().toLower();
    
    return SUPPORTED_VIDEO_FORMATS.contains(extension) || 
           SUPPORTED_AUDIO_FORMATS.contains(extension);
}

PlayerError VideoPlayer::mapMediaPlayerError(QMediaPlayer::Error error) const {
    switch (error) {
        case QMediaPlayer::NoError:
            return PlayerError::PlaybackFailed;  // Shouldn't happen...
        case QMediaPlayer::ResourceError:
            return PlayerError::MediaLoadFailed;
        case QMediaPlayer::FormatError:
            return PlayerError::InvalidMediaFormat;
        case QMediaPlayer::NetworkError:
            return PlayerError::NetworkError;
        case QMediaPlayer::AccessDeniedError:
            return PlayerError::ResourceNotAvailable;
        default:
            return PlayerError::PlaybackFailed;
    }
}

void VideoPlayer::handlePlaybackError(PlayerError error, const QString& description) {
    Logger::instance().error("Playback error: {}", description.toStdString());
    emit errorOccurred(error, description);
}

PlaybackState VideoPlayer::convertPlaybackState(QMediaPlayer::PlaybackState state) const {
    switch (state) {
        case QMediaPlayer::StoppedState:
            return PlaybackState::Stopped;
        case QMediaPlayer::PlayingState:
            return PlaybackState::Playing;
        case QMediaPlayer::PausedState:
            return PlaybackState::Paused;
        default:
            return PlaybackState::Stopped;
    }
}

MediaStatus VideoPlayer::convertMediaStatus(QMediaPlayer::MediaStatus status) const {
    switch (status) {
        case QMediaPlayer::NoMedia:
            return MediaStatus::NoMedia;
        case QMediaPlayer::LoadingMedia:
            return MediaStatus::Loading;
        case QMediaPlayer::LoadedMedia:
            return MediaStatus::Loaded;
        case QMediaPlayer::StalledMedia:
            return MediaStatus::Buffering;
        case QMediaPlayer::BufferingMedia:
            return MediaStatus::Buffering;
        case QMediaPlayer::BufferedMedia:
            return MediaStatus::Buffered;
        case QMediaPlayer::EndOfMedia:
            return MediaStatus::EndOfMedia;
        case QMediaPlayer::InvalidMedia:
            return MediaStatus::InvalidMedia;
        default:
            return MediaStatus::NoMedia;
    }
}

void VideoPlayer::savePerformanceMetrics() {
    if (!storageManager_ || mediaId_.isEmpty()) {
        return;
    }
    
    qint64 sessionDuration = performanceMetrics_.sessionStart.msecsTo(QDateTime::currentDateTime());
    
    Logger::instance().info("Session performance - Total Playback: {}ms, Session Duration: {}ms, Seeks: {}, Buffering: {}, Errors: {}",
                 performanceMetrics_.totalPlaybackTime,
                 sessionDuration,
                 performanceMetrics_.totalSeeks,
                 performanceMetrics_.bufferingEvents,
                 performanceMetrics_.errorCount);
}

void VideoPlayer::updatePerformanceMetrics() {
    // The main logic is inside onMediaPlayerStateChanged
}

Expected<bool, PlayerError> VideoPlayer::setAudioTrack(int trackId) {
    if (!mediaPlayer_) {
        return makeUnexpected(PlayerError::ResourceNotAvailable);
    }
    const auto& qAudioTracks = mediaPlayer_->audioTracks();
    if (trackId < 0 || trackId >= qAudioTracks.size()) {
        Logger::instance().warn("VideoPlayer: Invalid audio track ID: {}", trackId);
        return makeUnexpected(PlayerError::ResourceNotAvailable);
    }
    
    mediaPlayer_->setActiveAudioTrack(trackId);
    currentAudioTrack_ = trackId;
    emit currentAudioTrackChanged(trackId);
    
    Logger::instance().info("VideoPlayer: Selected audio track: {} ({})", 
                           trackId, audioTracks_[trackId].title.toStdString());
    return true;
}

Expected<bool, PlayerError> VideoPlayer::setSubtitleTrack(int trackId) {
    if (!mediaPlayer_) {
        return makeUnexpected(PlayerError::ResourceNotAvailable);
    }
    
    if (trackId == -1) {
        mediaPlayer_->setActiveSubtitleTrack(-1);
        currentSubtitleTrack_ = -1;
        emit currentSubtitleTrackChanged(-1);
        Logger::instance().info("VideoPlayer: Subtitles disabled");
        return true;
    }

    const auto& qSubtitleTracks = mediaPlayer_->subtitleTracks();
    if (trackId < 0 || trackId >= qSubtitleTracks.size()) {
        Logger::instance().warn("VideoPlayer: Invalid subtitle track ID: {}", trackId);
        return makeUnexpected(PlayerError::ResourceNotAvailable);
    }

    mediaPlayer_->setActiveSubtitleTrack(trackId);
    currentSubtitleTrack_ = trackId;
    emit currentSubtitleTrackChanged(trackId);
    
    Logger::instance().info("VideoPlayer: Selected subtitle track: {} ({})", 
                           trackId, subtitleTracks_[trackId].title.toStdString());
    return true;
}

Expected<bool, PlayerError> VideoPlayer::loadExternalSubtitles(const QString& filePath) {
    if (filePath.isEmpty()) {
        return makeUnexpected(PlayerError::SubtitleLoadFailed);
    }
    
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        Logger::instance().error("VideoPlayer: Subtitle file not found: {}", filePath.toStdString());
        return makeUnexpected(PlayerError::SubtitleLoadFailed);
    }
    
    QString extension = fileInfo.suffix().toLower();
    if (!SUPPORTED_SUBTITLE_FORMATS.contains(extension)) {
        Logger::instance().error("VideoPlayer: Unsupported subtitle format: {}", extension.toStdString());
        return makeUnexpected(PlayerError::SubtitleLoadFailed);
    }

    if (mediaPlayer_) {
        // Implement workaround: restart playback with subtitle URL set
        if (currentSource_.isEmpty()) {
            Logger::instance().warn("VideoPlayer: No current source to reload with subtitles");
            return makeUnexpected(PlayerError::ResourceNotAvailable);
        }
        
        // Save current playback state
        qint64 currentPosition = mediaPlayer_->position();
        PlaybackState currentState = currentPlaybackState_;
        bool wasPlaying = (currentState == PlaybackState::Playing);
        
        // Create subtitle track info for the external file
        SubtitleTrack externalTrack;
        externalTrack.id = subtitleTracks_.size();
        externalTrack.language = "external";
        externalTrack.title = fileInfo.baseName();
        externalTrack.codec = extension.toUpper();
        externalTrack.isDefault = false;
        externalTrack.isForced = false;
        externalTrack.filePath = filePath;
        
        // Add to subtitle tracks list
        subtitleTracks_.append(externalTrack);
        
        // Stop current playback
        mediaPlayer_->stop();
        
        // Create URL with subtitle parameter (platform-specific approach)
        QUrl sourceWithSubtitles = currentSource_;
        
        #ifdef Q_OS_WIN
            // Windows: Use subtitle file as fragment identifier
            sourceWithSubtitles.setFragment(QString("subtitle=%1").arg(filePath));
        #elif defined(Q_OS_MACOS)
            // macOS: Set subtitle as query parameter
            QUrlQuery query(sourceWithSubtitles);
            query.addQueryItem("subtitle", filePath);
            sourceWithSubtitles.setQuery(query);
        #else
            // Linux: Try query parameter approach
            QUrlQuery query(sourceWithSubtitles);
            query.addQueryItem("subtitle", filePath);
            sourceWithSubtitles.setQuery(query);
        #endif
        
        // Set the source with subtitle information
        mediaPlayer_->setSource(sourceWithSubtitles);
        
        // Wait a moment for the media to load, then restore position
        QTimer::singleShot(500, this, [this, currentPosition, wasPlaying]() {
            if (mediaPlayer_->mediaStatus() == QMediaPlayer::LoadedMedia ||
                mediaPlayer_->mediaStatus() == QMediaPlayer::BufferedMedia) {
                
                // Restore position
                mediaPlayer_->setPosition(currentPosition);
                
                // Resume playback if it was playing
                if (wasPlaying) {
                    mediaPlayer_->play();
                }
                
                Logger::instance().info("VideoPlayer: Restarted playback with external subtitles");
            }
        });
        
        emit subtitleTracksChanged(subtitleTracks_);
        Logger::instance().info("VideoPlayer: External subtitle loaded: {}", filePath.toStdString());
        return true;
    }
    
    return makeUnexpected(PlayerError::SubtitleLoadFailed);
}

} // namespace Murmur