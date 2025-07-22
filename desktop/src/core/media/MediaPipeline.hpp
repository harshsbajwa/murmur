#pragma once

#include <QObject>
#include <QFuture>
#include <QMutex>
#include <QString>
#include <QFileInfo>
#include <QTemporaryDir>
#include <unordered_map>
#include "../common/Expected.hpp"
#include "../common/RetryManager.hpp"
#include "../common/ErrorRecovery.hpp"


namespace Murmur {

// Forward declarations
class FFmpegWrapper;
class HardwareAccelerator;

// Forward declarations from FFmpegWrapper
enum class FFmpegError;
struct ProgressInfo;
struct ConversionOptions;
struct MediaFileInfo;
enum class HardwareAccel;

// Forward declarations from HardwareAccelerator
enum class HardwareType;

enum class MediaError {
    InvalidFile,
    UnsupportedFormat,
    ProcessingFailed,
    OutputError,
    ResourceExhausted,
    Cancelled
};

struct VideoInfo {
    QString filePath;
    QString format;
    qint64 duration;      // milliseconds
    qint64 fileSize;      // bytes
    int width;
    int height;
    double frameRate;
    QString codec;
    qint64 bitrate;
    bool hasAudio;
    QString audioCodec;
    int audioChannels;
    int audioSampleRate;
};

struct ConversionSettings {
    QString outputFormat = "mp4";
    QString videoCodec = "libx264";
    QString audioCodec = "aac";
    int videoBitrate = 2000;     // kbps
    int audioBitrate = 128;      // kbps
    int maxWidth = 1920;
    int maxHeight = 1080;
    bool extractAudio = false;
    bool preserveQuality = false;
    QString customOptions;
};

struct ConversionProgress {
    QString inputFile;
    QString outputFile;
    double percentage;
    qint64 processedFrames;
    qint64 totalFrames;
    double currentFps;
    qint64 elapsedTime;    // milliseconds
    qint64 estimatedTime;  // milliseconds remaining
    bool isCompleted;
    bool isCancelled;
};

/**
 * @brief High-performance media processing pipeline using FFmpeg
 * 
 * Provides video conversion, audio extraction, and format validation
 * with Qt integration and comprehensive error handling.
 */
class MediaPipeline : public QObject {
    Q_OBJECT

public:
    explicit MediaPipeline(QObject* parent = nullptr);
    ~MediaPipeline();

    // Video analysis
    QFuture<Expected<VideoInfo, MediaError>> analyzeVideo(const QString& filePath);
    
    // Format validation
    Expected<bool, MediaError> validateVideoFile(const QString& filePath);
    static QStringList supportedFormats();
    static QStringList supportedCodecs();
    
    // Video conversion
    QFuture<Expected<QString, MediaError>> convertVideo(
        const QString& inputPath,
        const QString& outputPath,
        const ConversionSettings& settings
    );
    
    // Audio extraction
    QFuture<Expected<QString, MediaError>> extractAudio(
        const QString& videoPath,
        const QString& outputPath,
        const QString& format = "wav"
    );
    
    // Thumbnail generation
    QFuture<Expected<QString, MediaError>> generateThumbnail(
        const QString& videoPath,
        const QString& outputPath,
        int timeOffset = 0  // seconds
    );
    
    // Operation management
    void cancelOperation(const QString& operationId);
    void cancelAllOperations();
    QStringList getActiveOperations() const;
    
    // Resource limits
    void setMaxConcurrentOperations(int maxOps);
    void setMemoryLimit(qint64 maxMemoryMB);
    void setTempDirectory(const QString& tempDir);

signals:
    void conversionProgress(const QString& operationId, const ConversionProgress& progress);
    void conversionCompleted(const QString& operationId, const QString& outputPath);
    void conversionFailed(const QString& operationId, MediaError error, const QString& errorString);
    void operationCancelled(const QString& operationId);

private slots:
    void onFFmpegOperationStarted(const QString& operationId, const QString& inputPath);
    void onFFmpegOperationProgress(const QString& operationId, const ProgressInfo& progress);
    void onFFmpegOperationCompleted(const QString& operationId, const QString& outputPath);
    void onFFmpegOperationFailed(const QString& operationId, FFmpegError error, const QString& errorMessage);
    void onFFmpegOperationCancelled(const QString& operationId);

private:
    struct OperationContext {
        QString id;
        QString inputFile;
        QString outputFile;
        ConversionSettings settings;
        QFileInfo inputInfo;
        qint64 startTime;
        qint64 totalFrames;
        bool isCancelled;
    };

    // FFmpeg integration
    std::unique_ptr<FFmpegWrapper> ffmpegWrapper_;
    std::unique_ptr<HardwareAccelerator> hardwareAccelerator_;
    
    // Error handling and recovery
    std::unique_ptr<ErrorRecovery> errorRecovery_;
    std::unique_ptr<RetryManager> retryManager_;
    
    // Conversion utilities
    ConversionOptions convertToFFmpegOptions(const ConversionSettings& settings);
    VideoInfo convertFromMediaFileInfo(const MediaFileInfo& info);
    MediaError convertFromFFmpegError(FFmpegError error);
    ConversionProgress createProgressFromFFmpeg(const QString& operationId, const ProgressInfo& ffmpegProgress);
    
    // Operation management
    QString generateOperationId();
    void cleanupOperation(const QString& operationId);
    bool checkResourceLimits() const;
    
    // Path and file utilities
    Expected<QString, MediaError> validateAndPreparePaths(
        const QString& inputPath,
        const QString& outputPath
    );
    
    // Error handling and recovery methods
    void setupErrorRecoveryStrategies();
    void initializeWithRetry();

    mutable QMutex operationsMutex_;
    std::unordered_map<QString, std::unique_ptr<OperationContext>> activeOperations_;
    QTemporaryDir tempDir_;
    
    // Configuration
    int maxConcurrentOperations_ = 4;
    qint64 maxMemoryMB_ = 2048;
    QString customTempDir_;
};

} // namespace Murmur