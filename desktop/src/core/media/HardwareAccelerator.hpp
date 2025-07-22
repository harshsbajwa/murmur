#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include "../common/Expected.hpp"

// Forward declare FFmpeg types
extern "C" {
    struct AVCodecContext;
    struct AVFrame;
    struct AVHWDeviceContext;
    struct AVBufferRef;
    #include <libavutil/hwcontext.h>
    #include <libavutil/pixfmt.h>
}

namespace Murmur {

enum class HardwareType {
    None,
    VideoToolbox,   // macOS Hardware Acceleration
    CUDA,           // NVIDIA GPU
    QSV,            // Intel Quick Sync Video
    VAAPI,          // Video Acceleration API (Intel/AMD on Linux)
    DXVA2,          // DirectX Video Acceleration (Windows)
    D3D11VA,        // Direct3D 11 Video Acceleration (Windows)
    VDPAU,          // Video Decode and Presentation API (NVIDIA on Linux)
    OpenCL,         // OpenCL acceleration
    Vulkan          // Vulkan GPU acceleration
};

enum class AcceleratorError {
    NotSupported,
    InitializationFailed,
    DeviceCreationFailed,
    ContextCreationFailed,
    FrameTransferFailed,
    IncompatibleFormat,
    DriverError,
    UnknownError
};

struct HardwareCapabilities {
    HardwareType type;
    QString name;
    QString description;
    bool supportsEncoding = false;
    bool supportsDecoding = false;
    QStringList supportedCodecs;
    QStringList supportedPixelFormats;
    int maxWidth = 0;
    int maxHeight = 0;
    bool isAvailable = false;
};

/**
 * @brief Hardware acceleration manager for FFmpeg operations
 * 
 * Provides unified interface for hardware acceleration across different platforms,
 * automatically detecting available hardware and optimizing performance.
 */
class HardwareAccelerator : public QObject {
    Q_OBJECT

public:
    explicit HardwareAccelerator(QObject* parent = nullptr);
    ~HardwareAccelerator() override;

    /**
     * @brief Initialize hardware acceleration detection
     * @return true if successful
     */
    Expected<bool, AcceleratorError> initialize();

    /**
     * @brief Get list of available hardware acceleration types
     * @return List of available hardware types
     */
    QList<HardwareType> getAvailableTypes() const;

    /**
     * @brief Get capabilities for specific hardware type
     * @param type Hardware acceleration type
     * @return Hardware capabilities or error
     */
    Expected<HardwareCapabilities, AcceleratorError> getCapabilities(HardwareType type) const;

    /**
     * @brief Get best hardware acceleration for given codec
     * @param codecName Codec name (e.g., "h264", "hevc")
     * @param isEncoding Whether this is for encoding (vs decoding)
     * @return Best hardware type or None if not available
     */
    HardwareType getBestHardwareForCodec(const QString& codecName, bool isEncoding = false) const;

    /**
     * @brief Create hardware device context
     * @param type Hardware acceleration type
     * @return Hardware device context or error
     */
    Expected<AVBufferRef*, AcceleratorError> createDeviceContext(HardwareType type);

    /**
     * @brief Setup hardware acceleration for codec context
     * @param codecContext Codec context to configure
     * @param type Hardware acceleration type
     * @return true if successful
     */
    Expected<bool, AcceleratorError> setupCodecHardware(AVCodecContext* codecContext, HardwareType type);

    /**
     * @brief Transfer frame from hardware to software
     * @param hwFrame Hardware frame
     * @param swFrame Software frame (output)
     * @return true if successful
     */
    Expected<bool, AcceleratorError> transferFrameToSoftware(AVFrame* hwFrame, AVFrame* swFrame);

    /**
     * @brief Transfer frame from software to hardware
     * @param swFrame Software frame
     * @param hwFrame Hardware frame (output)
     * @param type Hardware type
     * @return true if successful
     */
    Expected<bool, AcceleratorError> transferFrameToHardware(AVFrame* swFrame, AVFrame* hwFrame, HardwareType type);

    /**
     * @brief Check if hardware encoding is recommended for given parameters
     * @param codecName Codec name
     * @param width Video width
     * @param height Video height
     * @param bitrate Target bitrate
     * @return true if hardware encoding is recommended
     */
    bool isHardwareEncodingRecommended(const QString& codecName, int width, int height, int bitrate) const;

    /**
     * @brief Get platform-specific optimizations
     * @return List of recommended optimization flags
     */
    QStringList getPlatformOptimizations() const;

    /**
     * @brief Get hardware-specific encoder name
     * @param codecName Base codec name
     * @param type Hardware type
     * @return Hardware encoder name or empty if not available
     */
    QString getHardwareEncoderName(const QString& codecName, HardwareType type) const;

    /**
     * @brief Get hardware-specific decoder name
     * @param codecName Base codec name
     * @param type Hardware type
     * @return Hardware decoder name or empty if not available
     */
    QString getHardwareDecoderName(const QString& codecName, HardwareType type) const;

signals:
    void hardwareDetected(HardwareType type, const QString& name);
    void hardwareError(HardwareType type, AcceleratorError error, const QString& message);

private:
    struct HardwareAcceleratorPrivate;
    std::unique_ptr<HardwareAcceleratorPrivate> d;

    // Detection and initialization
    Expected<bool, AcceleratorError> detectAvailableHardware();
    Expected<bool, AcceleratorError> probeHardwareType(HardwareType type);
    Expected<HardwareCapabilities, AcceleratorError> queryHardwareCapabilities(HardwareType type);

    // Platform-specific detection
    Expected<bool, AcceleratorError> detectVideoToolbox();
    bool testVideoToolboxAvailability();
    Expected<bool, AcceleratorError> detectCUDA();
    Expected<bool, AcceleratorError> detectQSV();
    Expected<bool, AcceleratorError> detectVAAPI();
    Expected<bool, AcceleratorError> detectDXVA2();
    Expected<bool, AcceleratorError> detectD3D11VA();
    Expected<bool, AcceleratorError> detectVDPAU();

    // Hardware context management
    Expected<AVHWDeviceContext*, AcceleratorError> createHardwareDevice(HardwareType type);
    void releaseHardwareDevice(AVBufferRef* deviceRef);

    // Codec mapping
    QString mapCodecToHardware(const QString& codecName, HardwareType type, bool isEncoder) const;
    QStringList getSupportedCodecsForHardware(HardwareType type) const;

    // Performance analysis
    bool isPerformanceBeneficial(const QString& codecName, int width, int height, HardwareType type) const;
    int estimateHardwarePerformance(HardwareType type, const QString& codecName) const;

    // Error handling
    AcceleratorError mapFFmpegError(int averror) const;
    QString translateAcceleratorError(AcceleratorError error) const;

    // Platform detection
    bool isMacOS() const;
    bool isWindows() const;
    bool isLinux() const;
    QString getPlatformName() const;

    // Utility functions
    HardwareType stringToHardwareType(const QString& name) const;
    QString hardwareTypeToString(HardwareType type) const;
    AVHWDeviceType hardwareTypeToAVType(HardwareType type) const;
    HardwareType avTypeToHardwareType(AVHWDeviceType avType) const;
};

} // namespace Murmur