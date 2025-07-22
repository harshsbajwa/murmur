#pragma once

#include <memory>
#include <functional>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QMutex>
#include <QFuture>
#include <QtConcurrent>

#include "../common/Expected.hpp"

// Forward declare FFmpeg types
extern "C" {
    struct AVFormatContext;
    struct AVCodecContext;
    struct AVFrame;
    struct AVPacket;
    struct SwsContext;
    struct SwrContext;
    struct AVStream;
    struct AVCodec;
    struct AVDictionary;
    struct AVFilterGraph;
    struct AVFilterContext;
    
    // FFmpeg enums - will be defined in .cpp file
}

namespace Murmur {

enum class FFmpegError {
    InvalidFile,
    UnsupportedFormat,
    InitializationFailed,
    DecodingFailed,
    EncodingFailed,
    FilteringFailed,
    AllocationFailed,
    IOError,
    InvalidParameters,
    HardwareError,
    CancellationRequested,
    TimeoutError,
    UnknownError
};

enum class HardwareAccel {
    None,
    Auto,
    VideoToolbox,   // macOS
    CUDA,           // NVIDIA
    VAAPI,          // Intel/AMD Linux
    QSV,            // Intel Quick Sync
    DXVA2,          // Windows DirectX
    D3D11VA         // Windows Direct3D 11
};

struct VideoStreamInfo {
    int streamIndex = -1;
    QString codec;
    int width = 0;
    int height = 0;
    double frameRate = 0.0;
    qint64 bitrate = 0;
    qint64 frameCount = 0;
    double duration = 0.0;   // seconds
    QString pixelFormat;
    QString profile;
    QString level;
    bool hasAudioStream = false;
};

struct AudioStreamInfo {
    int streamIndex = -1;
    QString codec;
    int sampleRate = 0;
    int channels = 0;
    qint64 bitrate = 0;
    double duration = 0.0;   // seconds
    QString sampleFormat;
    QString channelLayout;
};

struct MediaFileInfo {
    QString filePath;
    QString format;
    qint64 fileSize = 0;
    double duration = 0.0;   // seconds
    qint64 bitrate = 0;
    VideoStreamInfo video;
    AudioStreamInfo audio;
    QStringList metadata;
    bool isValid = false;
    
    // Convenience accessors for test compatibility
    int width() const { return video.width; }
    int height() const { return video.height; }
    QString videoCodec() const { return video.codec; }
    QString audioCodec() const { return audio.codec; }
};

struct ConversionOptions {
    // Video options
    QString videoCodec = "libx264";
    int videoBitrate = 2000;        // kbps
    int width = 0;                  // 0 = keep original
    int height = 0;                 // 0 = keep original
    double frameRate = 0.0;         // 0 = keep original
    QString pixelFormat = "yuv420p";
    QString preset = "medium";      // ultrafast, fast, medium, slow, veryslow
    int crf = 23;                   // 0-51, lower = better quality
    
    // Audio options
    QString audioCodec = "aac";
    int audioBitrate = 128;         // kbps
    int audioSampleRate = 0;        // 0 = keep original
    int audioChannels = 0;          // 0 = keep original
    
    // Hardware acceleration
    HardwareAccel hwAccel = HardwareAccel::Auto;
    
    // Container options
    QString containerFormat = "mp4";
    
    // Advanced options
    QStringList customFilters;
    bool twoPass = false;
    bool preserveMetadata = true;
    bool fastStart = true;          // Move moov atom to beginning for web playback
    
    // Processing options
    int maxThreads = 0;             // 0 = auto-detect
    bool enableNvenc = true;        // Enable NVIDIA encoding if available
    bool enableQsv = true;          // Enable Intel Quick Sync if available
};

struct ProgressInfo {
    QString operationId;
    double progressPercent = 0.0;   // 0.0 to 100.0
    qint64 processedFrames = 0;
    qint64 totalFrames = 0;
    double currentFps = 0.0;
    qint64 elapsedTimeMs = 0;
    qint64 estimatedTimeMs = 0;
    qint64 processedBytes = 0;
    qint64 totalBytes = 0;
    bool isCompleted = false;
    QString currentPhase;           // "analyzing", "encoding", "finalizing"
};

// Progress callback function type
using FFmpegProgressCallback = std::function<void(const ProgressInfo&)>;
using CompletionCallback = std::function<void(const QString&, const Expected<QString, FFmpegError>&)>;

/**
 * @brief FFmpeg wrapper for high-performance media processing
 * 
 * This class provides direct C++ integration with FFmpeg libraries,
 * supporting hardware acceleration, advanced filtering, and real-time processing
 * with comprehensive error handling and progress tracking.
 */
class FFmpegWrapper : public QObject {
    Q_OBJECT

public:
    explicit FFmpegWrapper(QObject* parent = nullptr);
    ~FFmpegWrapper() override;

    // Non-copyable, non-movable
    FFmpegWrapper(const FFmpegWrapper&) = delete;
    FFmpegWrapper& operator=(const FFmpegWrapper&) = delete;
    FFmpegWrapper(FFmpegWrapper&&) = delete;
    FFmpegWrapper& operator=(FFmpegWrapper&&) = delete;

    /**
     * @brief Initialize FFmpeg libraries and hardware acceleration
     * @return true if successful, false otherwise
     */
    Expected<bool, FFmpegError> initialize();

    /**
     * @brief Analyze media file and extract metadata
     * @param filePath Path to media file
     * @return Media file information or error
     */
    QFuture<Expected<MediaFileInfo, FFmpegError>> analyzeFile(const QString& filePath);

    /**
     * @brief Convert video file with comprehensive options
     * @param inputPath Input file path
     * @param outputPath Output file path
     * @param options Conversion options
     * @param progressCallback Optional progress callback
     * @return Future with output path or error
     */
    QFuture<Expected<QString, FFmpegError>> convertVideo(
        const QString& inputPath,
        const QString& outputPath,
        const ConversionOptions& options = ConversionOptions{},
        FFmpegProgressCallback progressCallback = nullptr
    );

    /**
     * @brief Extract audio from video file
     * @param inputPath Input video file path
     * @param outputPath Output audio file path
     * @param options Audio extraction options
     * @return Future with output path or error
     */
    QFuture<Expected<QString, FFmpegError>> extractAudio(
        const QString& inputPath,
        const QString& outputPath,
        const ConversionOptions& options = ConversionOptions{}
    );

    /**
     * @brief Generate thumbnail from video
     * @param inputPath Input video file path
     * @param outputPath Output image file path
     * @param timeSeconds Time position for thumbnail (seconds)
     * @param width Thumbnail width (0 = original)
     * @param height Thumbnail height (0 = original)
     * @return Future with output path or error
     */
    QFuture<Expected<QString, FFmpegError>> generateThumbnail(
        const QString& inputPath,
        const QString& outputPath,
        double timeSeconds = 10.0,
        int width = 0,
        int height = 0
    );

    /**
     * @brief Extract frames from video at specific intervals
     * @param inputPath Input video file path
     * @param outputDir Output directory for frames
     * @param intervalSeconds Interval between frames in seconds
     * @param format Output image format (jpg, png, etc.)
     * @return Future with list of generated frame paths or error
     */
    QFuture<Expected<QStringList, FFmpegError>> extractFrames(
        const QString& inputPath,
        const QString& outputDir,
        double intervalSeconds = 1.0,
        const QString& format = "jpg"
    );

    /**
     * @brief Apply video filters (resize, crop, rotate, etc.)
     * @param inputPath Input file path
     * @param outputPath Output file path
     * @param filterGraph FFmpeg filter graph string
     * @param options Additional conversion options
     * @return Future with output path or error
     */
    QFuture<Expected<QString, FFmpegError>> applyFilters(
        const QString& inputPath,
        const QString& outputPath,
        const QString& filterGraph,
        const ConversionOptions& options = ConversionOptions{}
    );

    /**
     * @brief Cancel ongoing operation
     * @param operationId Operation ID to cancel
     */
    void cancelOperation(const QString& operationId);

    /**
     * @brief Cancel all ongoing operations
     */
    void cancelAllOperations();

    /**
     * @brief Get list of active operation IDs
     * @return List of operation IDs
     */
    QStringList getActiveOperations() const;

    /**
     * @brief Check if hardware acceleration is available
     * @param hwAccel Hardware acceleration type
     * @return true if available
     */
    bool isHardwareAccelAvailable(HardwareAccel hwAccel) const;

    /**
     * @brief Get available hardware acceleration methods
     * @return List of available hardware acceleration types
     */
    QList<HardwareAccel> getAvailableHardwareAccel() const;

    /**
     * @brief Get supported input formats
     * @return List of supported format names
     */
    static QStringList getSupportedInputFormats();

    /**
     * @brief Get supported output formats
     * @return List of supported format names
     */
    static QStringList getSupportedOutputFormats();

    /**
     * @brief Get supported video codecs
     * @return List of supported codec names
     */
    static QStringList getSupportedVideoCodecs();

    /**
     * @brief Get supported audio codecs
     * @return List of supported codec names
     */
    static QStringList getSupportedAudioCodecs();

    /**
     * @brief Get FFmpeg version information
     * @return Version string
     */
    static QString getFFmpegVersion();

    /**
     * @brief Validate file format and codec compatibility
     * @param filePath File to validate
     * @return true if valid and supported
     */
    static Expected<bool, FFmpegError> validateFile(const QString& filePath);

signals:
    void operationStarted(const QString& operationId, const QString& inputPath);
    void operationProgress(const QString& operationId, const ProgressInfo& progress);
    void operationCompleted(const QString& operationId, const QString& outputPath);
    void operationFailed(const QString& operationId, FFmpegError error, const QString& errorMessage);
    void operationCancelled(const QString& operationId);

private:
    struct FFmpegWrapperPrivate;
    std::unique_ptr<FFmpegWrapperPrivate> d;
    void waitForAllOperations();

    // Core FFmpeg operations
    Expected<bool, FFmpegError> initializeLibraries();
    Expected<bool, FFmpegError> detectHardwareAcceleration();
    void shutdownLibraries();
    
    // Video and audio processing
    Expected<QString, FFmpegError> performVideoConversion(const QString& operationId);
    Expected<QString, FFmpegError> performAudioExtraction(
        const QString& inputPath,
        const QString& outputPath,
        const ConversionOptions& options
    );
    
    // Audio frame buffering for fixed frame size encoders
    std::vector<AVFrame*> bufferAudioFrame(struct OperationContext* context, AVFrame* inputFrame);

    // Format context management
    Expected<AVFormatContext*, FFmpegError> openInputFile(const QString& filePath);
    Expected<AVFormatContext*, FFmpegError> createOutputFile(const QString& filePath, const QString& format);
    void closeFormatContext(AVFormatContext* context);

    // Codec management
    Expected<AVCodecContext*, FFmpegError> createVideoEncoder(
        const ConversionOptions& options,
        const VideoStreamInfo& inputInfo
    );
    Expected<AVCodecContext*, FFmpegError> createAudioEncoder(
        const ConversionOptions& options,
        const AudioStreamInfo& inputInfo
    );
    Expected<AVCodecContext*, FFmpegError> createVideoDecoder(AVStream* stream);
    Expected<AVCodecContext*, FFmpegError> createAudioDecoder(AVStream* stream);

    // Hardware acceleration
    Expected<AVCodecContext*, FFmpegError> setupHardwareAcceleration(
        AVCodecContext* codecContext,
        HardwareAccel hwAccel
    );
    Expected<bool, FFmpegError> initializeHardwareFrame(AVFrame* frame, HardwareAccel hwAccel);

    // Frame processing
    Expected<AVFrame*, FFmpegError> allocateFrame(int pixelFormat, int width, int height);
    Expected<AVFrame*, FFmpegError> allocateAudioFrame(int sampleFormat, int channels, int sampleRate);
    Expected<bool, FFmpegError> convertFrame(AVFrame* srcFrame, AVFrame* dstFrame, SwsContext* swsContext);
    Expected<bool, FFmpegError> convertAudioFrame(AVFrame* srcFrame, AVFrame* dstFrame, SwrContext* swrContext);

    // Filtering
    Expected<AVFilterGraph*, FFmpegError> createFilterGraph(
        const QString& filterDesc,
        const VideoStreamInfo& inputInfo,
        const ConversionOptions& options
    );
    Expected<bool, FFmpegError> processFrameThroughFilter(
        AVFrame* inputFrame,
        AVFrame* outputFrame,
        AVFilterGraph* filterGraph
    );

    // Progress tracking
    QString generateOperationId();
    void updateProgress(const QString& operationId, const ProgressInfo& progress);
    ProgressInfo calculateProgress(
        const QString& operationId,
        qint64 processedFrames,
        qint64 totalFrames,
        qint64 startTime
    );

    // Error handling
    FFmpegError mapAVError(int averror) const;
    QString translateFFmpegError(FFmpegError error) const;
    QString getAVErrorString(int averror) const;

    // Utility functions
    VideoStreamInfo extractVideoStreamInfo(AVStream* stream, AVCodecContext* codecContext);
    AudioStreamInfo extractAudioStreamInfo(AVStream* stream, AVCodecContext* codecContext);
    Expected<int, FFmpegError> findBestVideoStream(AVFormatContext* formatContext);
    Expected<int, FFmpegError> findBestAudioStream(AVFormatContext* formatContext);

    // Validation
    Expected<bool, FFmpegError> validateConversionOptions(const ConversionOptions& options);
    Expected<bool, FFmpegError> validateFilePath(const QString& filePath, bool mustExist = true);

    // Image encoding
    bool saveFrameAsImage(AVFrame* frame, const QString& outputPath, const QString& format);

    // Resource cleanup
    void cleanupOperation(const QString& operationId);
    void cleanupFFmpegResources();
};

} // namespace Murmur