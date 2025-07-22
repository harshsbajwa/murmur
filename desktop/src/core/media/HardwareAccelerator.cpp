#include "HardwareAccelerator.hpp"
#include "../common/Logger.hpp"

#include <QtCore/QSysInfo>
#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QOpenGLContext>

// FFmpeg includes
extern "C" {
#include <libavutil/hwcontext.h>
#ifdef Q_OS_MACOS
#include <libavutil/hwcontext_videotoolbox.h>
#include <VideoToolbox/VideoToolbox.h>
#include <CoreVideo/CoreVideo.h>
#endif
#ifdef Q_OS_WIN
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/hwcontext_dxva2.h>
#include <libavutil/hwcontext_qsv.h>
#endif
#ifdef Q_OS_LINUX
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/hwcontext_vdpau.h>
#include <libavutil/hwcontext_qsv.h>
#endif
// Only include CUDA if explicitly enabled
#ifdef HAVE_CUDA
#include <libavutil/hwcontext_cuda.h>
#endif
#include <libavutil/hwcontext_opencl.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
}

#ifdef Q_OS_MACOS
#include <VideoToolbox/VideoToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <d3d11.h>
#include <dxva2api.h>
#endif

#ifdef Q_OS_LINUX
#include <va/va.h>
#include <va/va_drm.h>
#endif

namespace Murmur {

struct HardwareAccelerator::HardwareAcceleratorPrivate {
    QHash<HardwareType, HardwareCapabilities> capabilities;
    QHash<HardwareType, AVBufferRef*> deviceContexts;
    QMutex hardwareMutex;
    bool initialized = false;
    
    // Performance thresholds
    static constexpr int MIN_HARDWARE_WIDTH = 720;
    static constexpr int MIN_HARDWARE_HEIGHT = 480;
    static constexpr int MIN_HARDWARE_BITRATE = 1000; // kbps
};

HardwareAccelerator::HardwareAccelerator(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<HardwareAcceleratorPrivate>()) {
    
    Logger::instance().info("Hardware accelerator initialized");
}

HardwareAccelerator::~HardwareAccelerator() {
    // Cleanup hardware device contexts
    QMutexLocker locker(&d->hardwareMutex);
    for (auto it = d->deviceContexts.begin(); it != d->deviceContexts.end(); ++it) {
        if (it.value()) {
            av_buffer_unref(&it.value());
        }
    }
    d->deviceContexts.clear();
    
    Logger::instance().info("Hardware accelerator destroyed");
}

Expected<bool, AcceleratorError> HardwareAccelerator::initialize() {
    if (d->initialized) {
        return true;
    }
    
    auto detectResult = detectAvailableHardware();
    if (detectResult.hasError()) {
        Logger::instance().warn("Hardware detection failed, continuing with software-only");
        // Don't return error - software fallback is always available
    }
    
    d->initialized = true;
    
    QStringList availableTypes;
    for (auto it = d->capabilities.begin(); it != d->capabilities.end(); ++it) {
        if (it.value().isAvailable) {
            availableTypes << hardwareTypeToString(it.key());
        }
    }
    
    Logger::instance().info("Hardware acceleration initialized. Available: {}", availableTypes.join(", ").toStdString());
    
    return true;
}

QList<HardwareType> HardwareAccelerator::getAvailableTypes() const {
    QMutexLocker locker(&d->hardwareMutex);
    QList<HardwareType> types;
    
    for (auto it = d->capabilities.begin(); it != d->capabilities.end(); ++it) {
        if (it.value().isAvailable) {
            types.append(it.key());
        }
    }
    
    return types;
}

Expected<HardwareCapabilities, AcceleratorError> HardwareAccelerator::getCapabilities(HardwareType type) const {
    QMutexLocker locker(&d->hardwareMutex);
    
    auto it = d->capabilities.find(type);
    if (it == d->capabilities.end()) {
        return makeUnexpected(AcceleratorError::NotSupported);
    }
    
    return it.value();
}

HardwareType HardwareAccelerator::getBestHardwareForCodec(const QString& codecName, bool isEncoding) const {
    QMutexLocker locker(&d->hardwareMutex);
    
    // Priority order based on platform and performance
    QList<HardwareType> priorityList;
    
    if (isMacOS()) {
        priorityList = {HardwareType::VideoToolbox, HardwareType::OpenCL};
    } else if (isWindows()) {
        priorityList = {HardwareType::D3D11VA, HardwareType::DXVA2, 
#ifdef HAVE_CUDA
                        HardwareType::CUDA, 
#endif
                        HardwareType::QSV};
    } else if (isLinux()) {
        priorityList = {HardwareType::VAAPI, 
#ifdef HAVE_CUDA
                        HardwareType::CUDA, 
#endif
                        HardwareType::VDPAU, HardwareType::QSV};
    }
    
    for (HardwareType type : priorityList) {
        auto it = d->capabilities.find(type);
        if (it != d->capabilities.end() && it.value().isAvailable) {
            bool supportsOperation = isEncoding ? it.value().supportsEncoding : it.value().supportsDecoding;
            if (supportsOperation && it.value().supportedCodecs.contains(codecName)) {
                return type;
            }
        }
    }
    
    return HardwareType::None;
}

Expected<AVBufferRef*, AcceleratorError> HardwareAccelerator::createDeviceContext(HardwareType type) {
    QMutexLocker locker(&d->hardwareMutex);
    
    // Check if we already have a device context for this type
    auto it = d->deviceContexts.find(type);
    if (it != d->deviceContexts.end() && it.value()) {
        av_buffer_ref(it.value()); // Increase reference count
        return it.value();
    }
    
    // Create new device context
    AVHWDeviceType avType = hardwareTypeToAVType(type);
    if (avType == AV_HWDEVICE_TYPE_NONE) {
        return makeUnexpected(AcceleratorError::NotSupported);
    }
    
    AVBufferRef* deviceRef = nullptr;
    int ret = av_hwdevice_ctx_create(&deviceRef, avType, nullptr, nullptr, 0);
    
    if (ret < 0) {
        Logger::instance().error("Failed to create hardware device context for {}: {}", hardwareTypeToString(type).toStdString(), ret);
        return makeUnexpected(AcceleratorError::DeviceCreationFailed);
    }
    
    d->deviceContexts[type] = deviceRef;
    
    Logger::instance().info("Created hardware device context: {}", hardwareTypeToString(type).toStdString());
    
    return deviceRef;
}

Expected<bool, AcceleratorError> HardwareAccelerator::setupCodecHardware(AVCodecContext* codecContext, HardwareType type) {
    if (!codecContext) {
        return makeUnexpected(AcceleratorError::InitializationFailed);
    }
    
    auto deviceResult = createDeviceContext(type);
    if (deviceResult.hasError()) {
        return makeUnexpected(deviceResult.error());
    }
    
    AVBufferRef* deviceRef = deviceResult.value();
    codecContext->hw_device_ctx = av_buffer_ref(deviceRef);
    
    // Set hardware pixel format
    switch (type) {
        case HardwareType::VideoToolbox:
            codecContext->pix_fmt = AV_PIX_FMT_VIDEOTOOLBOX;
            break;
#ifdef HAVE_CUDA
        case HardwareType::CUDA:
            codecContext->pix_fmt = AV_PIX_FMT_CUDA;
            break;
#endif
        case HardwareType::QSV:
            codecContext->pix_fmt = AV_PIX_FMT_QSV;
            break;
        case HardwareType::VAAPI:
            codecContext->pix_fmt = AV_PIX_FMT_VAAPI;
            break;
        case HardwareType::DXVA2:
            codecContext->pix_fmt = AV_PIX_FMT_DXVA2_VLD;
            break;
        case HardwareType::D3D11VA:
            codecContext->pix_fmt = AV_PIX_FMT_D3D11;
            break;
        case HardwareType::VDPAU:
            codecContext->pix_fmt = AV_PIX_FMT_VDPAU;
            break;
        default:
            break;
    }
    
    Logger::instance().info("Setup hardware acceleration for codec: {}", hardwareTypeToString(type).toStdString());
    
    return true;
}

Expected<bool, AcceleratorError> HardwareAccelerator::transferFrameToSoftware(AVFrame* hwFrame, AVFrame* swFrame) {
    if (!hwFrame || !swFrame) {
        return makeUnexpected(AcceleratorError::InitializationFailed);
    }
    
    int ret = av_hwframe_transfer_data(swFrame, hwFrame, 0);
    if (ret < 0) {
        Logger::instance().error("Failed to transfer frame to software: {}", ret);
        return makeUnexpected(AcceleratorError::FrameTransferFailed);
    }
    
    return true;
}

Expected<bool, AcceleratorError> HardwareAccelerator::transferFrameToHardware(AVFrame* swFrame, AVFrame* hwFrame, HardwareType type) {
    if (!swFrame || !hwFrame) {
        return makeUnexpected(AcceleratorError::InitializationFailed);
    }
    
    auto deviceResult = createDeviceContext(type);
    if (deviceResult.hasError()) {
        return makeUnexpected(deviceResult.error());
    }
    
    int ret = av_hwframe_transfer_data(hwFrame, swFrame, 0);
    if (ret < 0) {
        Logger::instance().error("Failed to transfer frame to hardware: {}", ret);
        return makeUnexpected(AcceleratorError::FrameTransferFailed);
    }
    
    return true;
}

bool HardwareAccelerator::isHardwareEncodingRecommended(const QString& codecName, int width, int height, int bitrate) const {
    // Hardware encoding is generally recommended for:
    // 1. High resolution content (720p+)
    // 2. High bitrate content (1Mbps+)
    // 3. Supported codecs (H.264, H.265/HEVC)
    
    if (width < d->MIN_HARDWARE_WIDTH || height < d->MIN_HARDWARE_HEIGHT) {
        return false;
    }
    
    if (bitrate < d->MIN_HARDWARE_BITRATE) {
        return false;
    }
    
    QStringList hardwareOptimalCodecs = {"h264", "hevc", "h265", "vp8", "vp9"};
    if (!hardwareOptimalCodecs.contains(codecName.toLower())) {
        return false;
    }
    
    // Check if any hardware acceleration is available for this codec
    HardwareType bestHw = getBestHardwareForCodec(codecName, true);
    return (bestHw != HardwareType::None);
}

QStringList HardwareAccelerator::getPlatformOptimizations() const {
    QStringList optimizations;
    
    if (isMacOS()) {
        optimizations << "-allow_sw" << "1";
        optimizations << "-realtime" << "1";
    } else if (isWindows()) {
        optimizations << "-hwaccel_output_format" << "d3d11";
    } else if (isLinux()) {
        optimizations << "-hwaccel_output_format" << "vaapi";
    }
    
    return optimizations;
}

QString HardwareAccelerator::getHardwareEncoderName(const QString& codecName, HardwareType type) const {
    return mapCodecToHardware(codecName, type, true);
}

QString HardwareAccelerator::getHardwareDecoderName(const QString& codecName, HardwareType type) const {
    return mapCodecToHardware(codecName, type, false);
}

Expected<bool, AcceleratorError> HardwareAccelerator::detectAvailableHardware() {
    QMutexLocker locker(&d->hardwareMutex);
    
    // Platform-specific detection
    if (isMacOS()) {
        detectVideoToolbox();
    }
    
    if (isWindows()) {
        detectD3D11VA();
        detectDXVA2();
    }
    
    if (isLinux()) {
        detectVAAPI();
        detectVDPAU();
    }
    
    // Cross-platform detection
#ifdef HAVE_CUDA
    detectCUDA();
#endif
    detectQSV();
    
    // Emit signals for detected hardware
    for (auto it = d->capabilities.begin(); it != d->capabilities.end(); ++it) {
        if (it.value().isAvailable) {
            emit hardwareDetected(it.key(), it.value().name);
        }
    }
    
    return true;
}

Expected<bool, AcceleratorError> HardwareAccelerator::detectVideoToolbox() {
#ifdef Q_OS_MACOS
    HardwareCapabilities caps;
    caps.type = HardwareType::VideoToolbox;
    caps.name = "Apple VideoToolbox";
    caps.description = "Apple hardware acceleration framework";
    caps.supportsEncoding = true;
    caps.supportsDecoding = true;
    caps.supportedCodecs = {"h264", "hevc", "h265"};
    caps.supportedPixelFormats = {"videotoolbox", "nv12", "yuv420p"};
    caps.maxWidth = 4096;
    caps.maxHeight = 4096;
    
    // Test VideoToolbox availability by trying to create a compression session
    caps.isAvailable = testVideoToolboxAvailability();
    if (!caps.isAvailable) {
        Logger::instance().warn("VideoToolbox detected but not functional");
        return makeUnexpected(AcceleratorError::NotSupported);
    }
    
    d->capabilities[HardwareType::VideoToolbox] = caps;
    
    Logger::instance().info("VideoToolbox detected and available");
    return true;
#else
    return makeUnexpected(AcceleratorError::NotSupported);
#endif
}

#ifdef Q_OS_MACOS
bool HardwareAccelerator::testVideoToolboxAvailability() {
    // Try to create a minimal compression session to test VideoToolbox
    CVPixelBufferRef pixelBuffer = nullptr;
    VTCompressionSessionRef compressionSession = nullptr;
    
    // Create a minimal pixel buffer
    CVReturn result = CVPixelBufferCreate(
        kCFAllocatorDefault,
        320, 240, // Small test size
        kCVPixelFormatType_32BGRA,
        nullptr,
        &pixelBuffer
    );
    
    if (result != kCVReturnSuccess) {
        Logger::instance().warn("VideoToolbox test: Failed to create pixel buffer");
        return false;
    }
    
    // Try to create compression session
    result = VTCompressionSessionCreate(
        kCFAllocatorDefault,
        320, 240,
        kCMVideoCodecType_H264,
        nullptr, // encoderSpecification
        nullptr, // sourceImageBufferAttributes
        nullptr, // compressedDataAllocator
        nullptr, // outputCallback
        nullptr, // outputCallbackRefCon
        &compressionSession
    );
    
    bool isAvailable = (result == noErr && compressionSession != nullptr);
    
    // Cleanup
    if (compressionSession) {
        VTCompressionSessionInvalidate(compressionSession);
        CFRelease(compressionSession);
    }
    if (pixelBuffer) {
        CVPixelBufferRelease(pixelBuffer);
    }
    
    if (!isAvailable) {
        Logger::instance().warn("VideoToolbox test: Failed to create compression session, error: {}", result);
    }
    
    return isAvailable;
}
#else
bool HardwareAccelerator::testVideoToolboxAvailability() {
    return false;
}
#endif

#ifdef HAVE_CUDA
Expected<bool, AcceleratorError> HardwareAccelerator::detectCUDA() {
    HardwareCapabilities caps;
    caps.type = HardwareType::CUDA;
    caps.name = "NVIDIA CUDA";
    caps.description = "NVIDIA GPU acceleration";
    caps.supportsEncoding = true;
    caps.supportsDecoding = true;
    caps.supportedCodecs = {"h264", "hevc", "h265"};
    caps.supportedPixelFormats = {"cuda", "nv12"};
    caps.maxWidth = 8192;
    caps.maxHeight = 8192;
    
    // Test CUDA availability by trying to create a device context
    AVBufferRef* testDevice = nullptr;
    int ret = av_hwdevice_ctx_create(&testDevice, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
    caps.isAvailable = (ret >= 0);
    
    if (testDevice) {
        av_buffer_unref(&testDevice);
    }
    
    d->capabilities[HardwareType::CUDA] = caps;
    
    if (caps.isAvailable) {
        Logger::instance().info("CUDA detected and available");
    }
    
    return caps.isAvailable;
}
#endif

Expected<bool, AcceleratorError> HardwareAccelerator::detectQSV() {
    HardwareCapabilities caps;
    caps.type = HardwareType::QSV;
    caps.name = "Intel Quick Sync Video";
    caps.description = "Intel hardware acceleration";
    caps.supportsEncoding = true;
    caps.supportsDecoding = true;
    caps.supportedCodecs = {"h264", "hevc", "h265", "mpeg2", "vp8", "vp9"};
    caps.supportedPixelFormats = {"qsv", "nv12"};
    caps.maxWidth = 4096;
    caps.maxHeight = 4096;
    
    // Test QSV availability
    AVBufferRef* testDevice = nullptr;
    int ret = av_hwdevice_ctx_create(&testDevice, AV_HWDEVICE_TYPE_QSV, nullptr, nullptr, 0);
    caps.isAvailable = (ret >= 0);
    
    if (testDevice) {
        av_buffer_unref(&testDevice);
    }
    
    d->capabilities[HardwareType::QSV] = caps;
    
    if (caps.isAvailable) {
        Logger::instance().info("Intel QSV detected and available");
    }
    
    return caps.isAvailable;
}

Expected<bool, AcceleratorError> HardwareAccelerator::detectVAAPI() {
#ifdef Q_OS_LINUX
    HardwareCapabilities caps;
    caps.type = HardwareType::VAAPI;
    caps.name = "Video Acceleration API";
    caps.description = "VA-API hardware acceleration";
    caps.supportsEncoding = true;
    caps.supportsDecoding = true;
    caps.supportedCodecs = {"h264", "hevc", "h265", "mpeg2", "vp8", "vp9"};
    caps.supportedPixelFormats = {"vaapi", "nv12"};
    caps.maxWidth = 4096;
    caps.maxHeight = 4096;
    
    // Test VA-API availability
    AVBufferRef* testDevice = nullptr;
    int ret = av_hwdevice_ctx_create(&testDevice, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
    caps.isAvailable = (ret >= 0);
    
    if (testDevice) {
        av_buffer_unref(&testDevice);
    }
    
    d->capabilities[HardwareType::VAAPI] = caps;
    
    if (caps.isAvailable) {
        Logger::instance().info("VA-API detected and available");
    }
    
    return caps.isAvailable;
#else
    return makeUnexpected(AcceleratorError::NotSupported);
#endif
}

Expected<bool, AcceleratorError> HardwareAccelerator::detectDXVA2() {
#ifdef Q_OS_WIN
    HardwareCapabilities caps;
    caps.type = HardwareType::DXVA2;
    caps.name = "DirectX Video Acceleration 2.0";
    caps.description = "Microsoft DirectX hardware acceleration";
    caps.supportsEncoding = false;
    caps.supportsDecoding = true;
    caps.supportedCodecs = {"h264", "hevc", "mpeg2", "vc1"};
    caps.supportedPixelFormats = {"dxva2_vld", "nv12"};
    caps.maxWidth = 4096;
    caps.maxHeight = 4096;
    
    // Test DXVA2 availability
    AVBufferRef* testDevice = nullptr;
    int ret = av_hwdevice_ctx_create(&testDevice, AV_HWDEVICE_TYPE_DXVA2, nullptr, nullptr, 0);
    caps.isAvailable = (ret >= 0);
    
    if (testDevice) {
        av_buffer_unref(&testDevice);
    }
    
    d->capabilities[HardwareType::DXVA2] = caps;
    
    if (caps.isAvailable) {
        Logger::instance().info("DXVA2 detected and available");
    }
    
    return caps.isAvailable;
#else
    return makeUnexpected(AcceleratorError::NotSupported);
#endif
}

Expected<bool, AcceleratorError> HardwareAccelerator::detectD3D11VA() {
#ifdef Q_OS_WIN
    HardwareCapabilities caps;
    caps.type = HardwareType::D3D11VA;
    caps.name = "Direct3D 11 Video Acceleration";
    caps.description = "Microsoft Direct3D 11 hardware acceleration";
    caps.supportsEncoding = true;
    caps.supportsDecoding = true;
    caps.supportedCodecs = {"h264", "hevc", "h265", "vp9"};
    caps.supportedPixelFormats = {"d3d11", "nv12"};
    caps.maxWidth = 8192;
    caps.maxHeight = 8192;
    
    // Test D3D11VA availability
    AVBufferRef* testDevice = nullptr;
    int ret = av_hwdevice_ctx_create(&testDevice, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
    caps.isAvailable = (ret >= 0);
    
    if (testDevice) {
        av_buffer_unref(&testDevice);
    }
    
    d->capabilities[HardwareType::D3D11VA] = caps;
    
    if (caps.isAvailable) {
        Logger::instance().info("D3D11VA detected and available");
    }
    
    return caps.isAvailable;
#else
    return makeUnexpected(AcceleratorError::NotSupported);
#endif
}

Expected<bool, AcceleratorError> HardwareAccelerator::detectVDPAU() {
#ifdef Q_OS_LINUX
    HardwareCapabilities caps;
    caps.type = HardwareType::VDPAU;
    caps.name = "Video Decode and Presentation API";
    caps.description = "NVIDIA VDPAU hardware acceleration";
    caps.supportsEncoding = false;
    caps.supportsDecoding = true;
    caps.supportedCodecs = {"h264", "hevc", "mpeg2", "mpeg4"};
    caps.supportedPixelFormats = {"vdpau", "nv12"};
    caps.maxWidth = 4096;
    caps.maxHeight = 4096;
    
    // Test VDPAU availability
    AVBufferRef* testDevice = nullptr;
    int ret = av_hwdevice_ctx_create(&testDevice, AV_HWDEVICE_TYPE_VDPAU, nullptr, nullptr, 0);
    caps.isAvailable = (ret >= 0);
    
    if (testDevice) {
        av_buffer_unref(&testDevice);
    }
    
    d->capabilities[HardwareType::VDPAU] = caps;
    
    if (caps.isAvailable) {
        Logger::instance().info("VDPAU detected and available");
    }
    
    return caps.isAvailable;
#else
    return makeUnexpected(AcceleratorError::NotSupported);
#endif
}

QString HardwareAccelerator::mapCodecToHardware(const QString& codecName, HardwareType type, bool isEncoder) const {
    QString codec = codecName.toLower();
    QString suffix = isEncoder ? "_enc" : "_dec";
    
    switch (type) {
        case HardwareType::VideoToolbox:
            if (codec == "h264") return isEncoder ? "h264_videotoolbox" : "h264";
            if (codec == "hevc" || codec == "h265") return isEncoder ? "hevc_videotoolbox" : "hevc";
            break;
            
#ifdef HAVE_CUDA
        case HardwareType::CUDA:
            if (codec == "h264") return isEncoder ? "h264_nvenc" : "h264_cuvid";
            if (codec == "hevc" || codec == "h265") return isEncoder ? "hevc_nvenc" : "hevc_cuvid";
            break;
#endif
            
        case HardwareType::QSV:
            if (codec == "h264") return isEncoder ? "h264_qsv" : "h264_qsv";
            if (codec == "hevc" || codec == "h265") return isEncoder ? "hevc_qsv" : "hevc_qsv";
            break;
            
        case HardwareType::VAAPI:
            if (codec == "h264") return isEncoder ? "h264_vaapi" : "h264";
            if (codec == "hevc" || codec == "h265") return isEncoder ? "hevc_vaapi" : "hevc";
            break;
            
        default:
            break;
    }
    
    return QString(); // No hardware codec available
}

// Platform detection utilities
bool HardwareAccelerator::isMacOS() const {
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

bool HardwareAccelerator::isWindows() const {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool HardwareAccelerator::isLinux() const {
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}

QString HardwareAccelerator::getPlatformName() const {
    return QSysInfo::productType();
}

QString HardwareAccelerator::hardwareTypeToString(HardwareType type) const {
    switch (type) {
        case HardwareType::None: return "None";
        case HardwareType::VideoToolbox: return "VideoToolbox";
#ifdef HAVE_CUDA
        case HardwareType::CUDA: return "CUDA";
#endif
        case HardwareType::QSV: return "QSV";
        case HardwareType::VAAPI: return "VA-API";
        case HardwareType::DXVA2: return "DXVA2";
        case HardwareType::D3D11VA: return "D3D11VA";
        case HardwareType::VDPAU: return "VDPAU";
        case HardwareType::OpenCL: return "OpenCL";
        case HardwareType::Vulkan: return "Vulkan";
        default: return "Unknown";
    }
}

AVHWDeviceType HardwareAccelerator::hardwareTypeToAVType(HardwareType type) const {
    switch (type) {
        case HardwareType::VideoToolbox: return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#ifdef HAVE_CUDA
        case HardwareType::CUDA: return AV_HWDEVICE_TYPE_CUDA;
#endif
        case HardwareType::QSV: return AV_HWDEVICE_TYPE_QSV;
        case HardwareType::VAAPI: return AV_HWDEVICE_TYPE_VAAPI;
        case HardwareType::DXVA2: return AV_HWDEVICE_TYPE_DXVA2;
        case HardwareType::D3D11VA: return AV_HWDEVICE_TYPE_D3D11VA;
        case HardwareType::VDPAU: return AV_HWDEVICE_TYPE_VDPAU;
        case HardwareType::OpenCL: return AV_HWDEVICE_TYPE_OPENCL;
        case HardwareType::Vulkan: return AV_HWDEVICE_TYPE_VULKAN;
        default: return AV_HWDEVICE_TYPE_NONE;
    }
}

} // namespace Murmur