#pragma once

#include <QObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoSink>
#include <QUrl>
#include <QTimer>
#include <QMutex>
#include <QJsonObject>
#include <QSize>
#include <QEventLoop>
#include <QImage>
#include "../common/Expected.hpp"
#include "../storage/StorageManager.hpp"

namespace Murmur {

enum class PlayerError {
    MediaLoadFailed,
    PlaybackFailed,
    InvalidMediaFormat,
    ResourceNotAvailable,
    NetworkError,
    HardwareAccelerationFailed,
    SubtitleLoadFailed,
    AudioOutputFailed
};

enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
    Buffering,
    Seeking,
    Error
};

enum class MediaStatus {
    NoMedia,
    Loading,
    Loaded,
    Buffering,
    Buffered,
    EndOfMedia,
    InvalidMedia
};

struct PlaybackPosition {
    qint64 position;        // Current position in milliseconds
    qint64 duration;        // Total duration in milliseconds
    double rate;            // Playback rate (1.0 = normal speed)
    bool isSeekable;        // Whether seeking is supported
    QDateTime timestamp;    // When this position was recorded
};

struct VideoMetadata {
    QString title;
    QString description;
    qint64 duration;
    int width;
    int height;
    double frameRate;
    QString videoCodec;
    QString audioCodec;
    qint64 bitrate;
    QString format;
    QJsonObject customMetadata;
};

struct SubtitleTrack {
    int id;
    QString language;
    QString title;
    QString codec;
    bool isDefault;
    bool isForced;
    QString filePath;       // For external subtitles
};

struct AudioTrack {
    int id;
    QString language;
    QString title;
    QString codec;
    int channels;
    int sampleRate;
    bool isDefault;
};

/**
 * @brief Video player with Qt Multimedia integration
 * 
 * Provides high-performance video playback with subtitle support,
 * multiple audio tracks, hardware acceleration, and comprehensive
 * position tracking with database persistence.
 */
class VideoPlayer : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(PlaybackState playbackState READ playbackState NOTIFY playbackStateChanged)
    Q_PROPERTY(MediaStatus mediaStatus READ mediaStatus NOTIFY mediaStatusChanged)
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY hasAudioChanged)
    Q_PROPERTY(bool isSeekable READ isSeekable NOTIFY seekableChanged)
    Q_PROPERTY(QVideoSink* videoSink READ videoSink CONSTANT)

public:
    explicit VideoPlayer(QObject* parent = nullptr);
    ~VideoPlayer();

    // Core playback control
    QUrl source() const;
    void setSource(const QUrl& source);
    
    PlaybackState playbackState() const;
    MediaStatus mediaStatus() const;
    
    qint64 position() const;
    void setPosition(qint64 position);
    
    qint64 duration() const;
    
    double playbackRate() const;
    void setPlaybackRate(double rate);
    
    int volume() const;
    void setVolume(int volume);
    
    bool isMuted() const;
    void setMuted(bool muted);
    
    bool hasVideo() const;
    bool hasAudio() const;
    bool isSeekable() const;
    
    QVideoSink* videoSink() const;

    // Storage integration
    void setStorageManager(StorageManager* storage);
    void setMediaId(const QString& mediaId);
    QString mediaId() const;

    // Advanced features
    Expected<VideoMetadata, PlayerError> getMetadata() const;
    QList<AudioTrack> getAudioTracks() const;
    QList<SubtitleTrack> getSubtitleTracks() const;
    
    Expected<bool, PlayerError> setAudioTrack(int trackId);
    Expected<bool, PlayerError> setSubtitleTrack(int trackId);
    Expected<bool, PlayerError> loadExternalSubtitles(const QString& filePath);
    
    // Position management
    Expected<bool, PlayerError> savePosition();
    Expected<bool, PlayerError> restorePosition();
    void enableAutoSavePosition(bool enabled, int intervalMs = 5000);
    
    // Hardware acceleration
    void setHardwareAccelerationEnabled(bool enabled);
    bool isHardwareAccelerationEnabled() const;
    bool isHardwareAccelerationSupported() const;
    
    // Network streaming
    void setBufferSize(qint64 sizeBytes);
    void setNetworkCacheSize(qint64 sizeBytes);
    qint64 bufferedBytes() const;
    
    // Snapshot and thumbnails
    Expected<QString, PlayerError> captureSnapshot(const QString& outputPath);
    Expected<QList<QString>, PlayerError> generateThumbnails(
        const QString& outputDir, 
        int count = 10,
        const QSize& size = QSize(160, 90)
    );

public slots:
    void play();
    void pause();
    void stop();
    void togglePlayPause();
    
    // Seeking
    void seekForward(qint64 ms = 10000);   // 10 seconds
    void seekBackward(qint64 ms = 10000);  // 10 seconds
    void seekToPercentage(double percentage);
    
    // Frame-by-frame navigation
    void stepForward();
    void stepBackward();
    
    // Speed control
    void increaseSpeed();
    void decreaseSpeed();
    void resetSpeed();
    
    // Audio control
    void increaseVolume(int delta = 10);
    void decreaseVolume(int delta = 10);
    void toggleMute();

signals:
    // Core playback signals
    void sourceChanged(const QUrl& source);
    void playbackStateChanged(PlaybackState state);
    void mediaStatusChanged(MediaStatus status);
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void playbackRateChanged(double rate);
    void volumeChanged(int volume);
    void mutedChanged(bool muted);
    void hasVideoChanged(bool hasVideo);
    void hasAudioChanged(bool hasAudio);
    void seekableChanged(bool seekable);
    
    // Error and status signals
    void errorOccurred(PlayerError error, const QString& description);
    void bufferingProgressChanged(double progress);
    void networkStateChanged(const QString& state);
    
    // Track and subtitle signals
    void audioTracksChanged(const QList<AudioTrack>& tracks);
    void subtitleTracksChanged(const QList<SubtitleTrack>& tracks);
    void currentAudioTrackChanged(int trackId);
    void currentSubtitleTrackChanged(int trackId);
    
    // Advanced features
    void metadataChanged(const VideoMetadata& metadata);
    void snapshotCaptured(const QString& filePath);
    void thumbnailsGenerated(const QList<QString>& filePaths);
    void positionSaved(qint64 position);
    void positionRestored(qint64 position);

private slots:
    void onMediaPlayerStateChanged(QMediaPlayer::PlaybackState state);
    void onMediaPlayerStatusChanged(QMediaPlayer::MediaStatus status);
    void onMediaPlayerPositionChanged(qint64 position);
    void onMediaPlayerDurationChanged(qint64 duration);
    void onMediaPlayerError(QMediaPlayer::Error error, const QString& errorString);
    void onMediaPlayerBufferProgressChanged(float progress);
    void onMediaPlayerPlaybackRateChanged(qreal rate);
    void onMediaPlayerTracksChanged();
    void onAutoSaveTimer();

private:
    // Core Qt Multimedia components
    QMediaPlayer* mediaPlayer_;
    QAudioOutput* audioOutput_;
    QVideoSink* videoSink_;
    
    // Storage and persistence
    StorageManager* storageManager_ = nullptr;
    QString mediaId_;
    QTimer* autoSaveTimer_;
    bool autoSaveEnabled_ = true;
    
    // Current state
    QUrl currentSource_;
    PlaybackState currentPlaybackState_ = PlaybackState::Stopped;
    MediaStatus currentMediaStatus_ = MediaStatus::NoMedia;
    VideoMetadata currentMetadata_;
    QList<AudioTrack> audioTracks_;
    QList<SubtitleTrack> subtitleTracks_;
    int currentAudioTrack_ = -1;
    int currentSubtitleTrack_ = -1;
    
    // Settings
    bool hardwareAccelerationEnabled_ = true;
    qint64 networkCacheSize_ = 64 * 1024 * 1024;  // 64MB
    qint64 bufferSize_ = 8 * 1024 * 1024;         // 8MB
    
    // Performance tracking
    struct PerformanceMetrics {
        qint64 totalPlaybackTime = 0;
        qint64 totalSeeks = 0;
        qint64 bufferingEvents = 0;
        qint64 errorCount = 0;
        QDateTime sessionStart;
        QDateTime playbackStartTime;
    };
    PerformanceMetrics performanceMetrics_;
    
    // Thread safety
    mutable QMutex stateMutex_;
    
    // Helper methods
    void initializePlayer();
    void updateMetadata();
    void detectTracks();
    void savePerformanceMetrics();
    
    // Position persistence
    void persistCurrentPosition();
    Expected<qint64, PlayerError> loadSavedPosition();
    
    // Format support detection
    bool isFormatSupported(const QUrl& source) const;
    
    // Error handling
    PlayerError mapMediaPlayerError(QMediaPlayer::Error error) const;
    void handlePlaybackError(PlayerError error, const QString& description);
    
    // State conversion helpers
    PlaybackState convertPlaybackState(QMediaPlayer::PlaybackState state) const;
    MediaStatus convertMediaStatus(QMediaPlayer::MediaStatus status) const;
    
    // Utility methods
    void updatePerformanceMetrics();
    
    // Constants
    static const QStringList SUPPORTED_VIDEO_FORMATS;
    static const QStringList SUPPORTED_AUDIO_FORMATS;
    static const QStringList SUPPORTED_SUBTITLE_FORMATS;
    static const int DEFAULT_AUTO_SAVE_INTERVAL_MS = 5000;
    static const int MAX_BUFFER_SIZE_MB = 256;
    static const int MAX_CACHE_SIZE_MB = 512;
};

} // namespace Murmur