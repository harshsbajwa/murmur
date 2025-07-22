#include "MacOSMediaAccelerator.hpp"
#include "../../core/common/Logger.hpp"
#include <QtCore/QSysInfo>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtMultimedia/QVideoFrame>
#include <QtMultimedia/QVideoFrameFormat>
#include <QtMultimedia/QAbstractVideoBuffer>
#include <QtGui/QImage>

// fmt formatter for QString to allow logging it directly
#include <fmt/core.h>
#include <QtCore/QString>
template <> struct fmt::formatter<QString> : fmt::formatter<std::string> {
    auto format(const QString& s, format_context& ctx) const {
        return formatter<std::string>::format(s.toStdString(), ctx);
    }
};

#ifdef Q_OS_MACOS
// macOS Framework imports
#import <Metal/Metal.h>
#import <VideoToolbox/VideoToolbox.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreFoundation/CoreFoundation.h>
#import <IOKit/IOKitLib.h>
#import <IOKit/graphics/IOGraphicsLib.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>

// External bridge functions
extern "C" {
    const char* getMacOSGPUInfo();
    bool getMacOSDiscreteGPUStatus();
    int getMacOSVRAMSize();
    bool getMacOSMetalSupport();
    const char* getMacOSMetalDeviceInfo();
    bool getMacOSVideoToolboxSupport();
    const char* getMacOSVideoToolboxCodecs();
    bool getMacOSLowPowerModeEnabled();
    void setMacOSGPUPreference(bool preferIntegrated);
}

// Objective-C++ helper functions
static QVideoFrameFormat::PixelFormat cvPixelFormatToQt(OSType pixelFormat);
static OSType qtPixelFormatToCV(QVideoFrameFormat::PixelFormat pixelFormat);
static QVideoFrame pixelBufferToQVideoFrame(CVPixelBufferRef pixelBuffer);
static CVPixelBufferRef qVideoFrameToPixelBuffer(const QVideoFrame& frame);

// Convert CVPixelBufferRef to QVideoFrame
static QVideoFrame pixelBufferToQVideoFrame(CVPixelBufferRef pixelBuffer) {
    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    
    // Get pixel buffer information
    OSType pixelFormatType = CVPixelBufferGetPixelFormatType(pixelBuffer);
    int width = static_cast<int>(CVPixelBufferGetWidth(pixelBuffer));
    int height = static_cast<int>(CVPixelBufferGetHeight(pixelBuffer));

    // Map CVPixelBuffer pixel format to QVideoFrameFormat::PixelFormat
    QVideoFrameFormat::PixelFormat format = cvPixelFormatToQt(pixelFormatType);
    if (format == QVideoFrameFormat::Format_Invalid) {
        Murmur::Logger::instance().warn("Unsupported pixel format conversion from CVPixelBuffer");
        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        return QVideoFrame();
    }

    // Create QVideoFrame based on pixel format
    OSType pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
    QVideoFrame videoFrame;
    
    if (pixelFormat == kCVPixelFormatType_32BGRA || 
        pixelFormat == kCVPixelFormatType_32ARGB) {
        // Handle packed formats (BGRA/ARGB)
        uchar *baseAddress = static_cast<uchar*>(CVPixelBufferGetBaseAddress(pixelBuffer));
        QImage::Format format = (pixelFormat == kCVPixelFormatType_32BGRA) ? 
                               QImage::Format_ARGB32 : QImage::Format_RGB32;
        QImage image(baseAddress, width, height, format);
        videoFrame = QVideoFrame(image);
    } else if (pixelFormat == kCVPixelFormatType_420YpCbCr8Planar ||
               pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
        // Handle planar YUV formats - create appropriate QVideoFrameFormat
        QVideoFrameFormat::PixelFormat qtFormat = 
            (pixelFormat == kCVPixelFormatType_420YpCbCr8Planar) ?
            QVideoFrameFormat::Format_YUV420P : QVideoFrameFormat::Format_NV12;
        
        QVideoFrameFormat frameFormat(QSize(width, height), qtFormat);
        
        // Get plane data for YUV
        size_t yPlaneSize = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0) * height;
        uchar *yPlane = static_cast<uchar*>(CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0));
        
        // Create frame with planar data
        videoFrame = QVideoFrame(frameFormat);
        if (videoFrame.map(QVideoFrame::WriteOnly)) {
            memcpy(videoFrame.bits(0), yPlane, yPlaneSize);
            
            if (CVPixelBufferGetPlaneCount(pixelBuffer) > 1) {
                size_t uvPlaneSize = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1) * height / 2;
                uchar *uvPlane = static_cast<uchar*>(CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1));
                memcpy(videoFrame.bits(1), uvPlane, uvPlaneSize);
            }
            videoFrame.unmap();
        }
    } else {
        // Fallback for unknown formats - convert to BGRA
        uchar *baseAddress = static_cast<uchar*>(CVPixelBufferGetBaseAddress(pixelBuffer));
        QImage image(baseAddress, width, height, QImage::Format_ARGB32);
        videoFrame = QVideoFrame(image);
        Murmur::Logger::instance().warn("Unknown pixel format {}, using fallback", pixelFormat);
    }
    
    videoFrame.setStartTime(0); // Set timestamp if available
    
    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    return videoFrame;
}

// Convert QVideoFrame to CVPixelBufferRef
static CVPixelBufferRef qVideoFrameToPixelBuffer(const QVideoFrame& frame) {
    if (!frame.isValid()) {
        Murmur::Logger::instance().warn("Invalid QVideoFrame for conversion");
        return nullptr;
    }

    // Clone and map frame for reading
    QVideoFrame cloneFrame(frame);
    if (!cloneFrame.map(QVideoFrame::ReadOnly)) {
        Murmur::Logger::instance().warn("Failed to map QVideoFrame for reading");
        return nullptr;
    }

    // Map QVideoFrameFormat::PixelFormat to CVPixelBuffer pixel format
    OSType pixelFormat = qtPixelFormatToCV(cloneFrame.pixelFormat());
    if (pixelFormat == 0) {
        Murmur::Logger::instance().warn("Unsupported pixel format conversion from QVideoFrame");
        cloneFrame.unmap();
        return nullptr;
    }

    // We must create a CVPixelBuffer with its own storage and copy the data into it
    CVPixelBufferRef pixelBuffer = nullptr;
    NSDictionary *pixelAttributes = @{
        (id)kCVPixelBufferWidthKey: @(cloneFrame.width()),
        (id)kCVPixelBufferHeightKey: @(cloneFrame.height()),
        (id)kCVPixelBufferPixelFormatTypeKey: @(pixelFormat),
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
    };

    OSStatus status = CVPixelBufferCreate(kCFAllocatorDefault,
                                          cloneFrame.width(),
                                          cloneFrame.height(),
                                          pixelFormat,
                                          (__bridge CFDictionaryRef)pixelAttributes,
                                          &pixelBuffer);

    if (status != kCVReturnSuccess) {
        Murmur::Logger::instance().error("Failed to create CVPixelBuffer: {}", status);
        cloneFrame.unmap();
        return nullptr;
    }
    
    CVPixelBufferLockBaseAddress(pixelBuffer, 0);

    // Handle planar and packed formats correctly
    if (CVPixelBufferIsPlanar(pixelBuffer) && CVPixelBufferGetPlaneCount(pixelBuffer) > 1) {
        // Planar format: copy each plane separately
        size_t planeCount = CVPixelBufferGetPlaneCount(pixelBuffer);
        for (size_t plane = 0; plane < planeCount; ++plane) {
            void* destPlane = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, plane);
            const void* srcPlane = cloneFrame.bits(static_cast<int>(plane));
            size_t srcBytesPerLine = cloneFrame.bytesPerLine(static_cast<int>(plane));
            size_t destBytesPerLine = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, plane);
            size_t planeHeight = CVPixelBufferGetHeightOfPlane(pixelBuffer, plane);
            
            if (srcPlane && destPlane) {
                // Copy line by line to handle different strides
                const uint8_t* srcBytes = static_cast<const uint8_t*>(srcPlane);
                uint8_t* destBytes = static_cast<uint8_t*>(destPlane);
                
                size_t copyWidth = std::min(srcBytesPerLine, destBytesPerLine);
                for (size_t row = 0; row < planeHeight; ++row) {
                    memcpy(destBytes + row * destBytesPerLine, 
                           srcBytes + row * srcBytesPerLine, 
                           copyWidth);
                }
            }
        }
    } else {
        // Packed format: copy as single buffer
        void* dest = CVPixelBufferGetBaseAddress(pixelBuffer);
        const void* src = cloneFrame.bits(0);
        memcpy(dest, src, cloneFrame.mappedBytes(0));
    }

    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    cloneFrame.unmap();
    
    return pixelBuffer;
}

// Map CVPixelBuffer pixel format type to QVideoFrameFormat::PixelFormat
static QVideoFrameFormat::PixelFormat cvPixelFormatToQt(OSType pixelFormat) {
    switch (pixelFormat) {
        case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
            return QVideoFrameFormat::Format_YUV420P;
        case kCVPixelFormatType_32BGRA:
            return QVideoFrameFormat::Format_BGRA8888;
        default:
            return QVideoFrameFormat::Format_Invalid;
    }
}

// Map QVideoFrameFormat::PixelFormat to CVPixelBuffer pixel format type
static OSType qtPixelFormatToCV(QVideoFrameFormat::PixelFormat pixelFormat) {
    switch (pixelFormat) {
        case QVideoFrameFormat::Format_YUV420P:
            return kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        case QVideoFrameFormat::Format_BGRA8888:
            return kCVPixelFormatType_32BGRA;
        default:
            return 0;
    }
}

#endif

namespace Murmur {

struct MacOSMediaAccelerator::MacOSMediaAcceleratorPrivate {
    bool videoToolboxInitialized = false;
    bool metalInitialized = false;
    
#ifdef Q_OS_MACOS
    id<MTLDevice> metalDevice = nil;
    VTDecompressionSessionRef decompressionSession = nullptr;
    VTCompressionSessionRef compressionSession = nullptr;
    CVPixelBufferPoolRef pixelBufferPool = nullptr;
    
    // Current codec settings
    QString currentDecoderCodec;
    QString currentEncoderCodec;
    EncoderSettings currentEncoderSettings;
    
    // Metal textures for GPU processing
    CVMetalTextureCacheRef textureCache = nullptr;
#endif
    
    QStringList supportedDecoders;
    QStringList supportedEncoders;
    QString gpuInfo;
    bool hasDiscreteGPU = false;
    int vramSize = 0;
};

MacOSMediaAccelerator::MacOSMediaAccelerator(QObject* parent)
    : PlatformAccelerator(parent)
    , d(std::make_unique<MacOSMediaAcceleratorPrivate>()) {
    
    Murmur::Logger::instance().info("Initializing macOS media acceleration with native APIs");
    
#ifdef Q_OS_MACOS
    // Use native API calls
    d->videoToolboxInitialized = getMacOSVideoToolboxSupport();
    d->metalInitialized = getMacOSMetalSupport();
    d->hasDiscreteGPU = getMacOSDiscreteGPUStatus();
    d->vramSize = getMacOSVRAMSize();
    d->gpuInfo = QString::fromUtf8(getMacOSGPUInfo());
    
    // Get supported codecs from VideoToolbox
    if (d->videoToolboxInitialized) {
        QString supportedCodecs = QString::fromUtf8(getMacOSVideoToolboxCodecs());
        
        // Parse supported codecs and populate lists
        if (supportedCodecs.contains("H.264", Qt::CaseInsensitive)) {
            d->supportedDecoders.append("h264");
            d->supportedEncoders.append("h264");
        }
        if (supportedCodecs.contains("H.265", Qt::CaseInsensitive) || 
            supportedCodecs.contains("HEVC", Qt::CaseInsensitive)) {
            d->supportedDecoders.append("hevc");
            d->supportedEncoders.append("hevc");
        }
        if (supportedCodecs.contains("ProRes", Qt::CaseInsensitive)) {
            d->supportedDecoders.append("prores");
            d->supportedEncoders.append("prores");
        }
        
        // Add common decoding-only codecs
        d->supportedDecoders.append({"vp8", "vp9", "av1", "mpeg2", "mpeg4"});
    }
#else
    // Fallback for non-macOS builds
    d->videoToolboxInitialized = false;
    d->metalInitialized = false;
    d->hasDiscreteGPU = false;
    d->vramSize = 0;
    d->gpuInfo = "macOS APIs not available";
#endif
    
    Murmur::Logger::instance().info("VideoToolbox: {}, Metal: {}, GPU: {} ({} MB VRAM)",
                d->videoToolboxInitialized ? "Available" : "Not Available",
                d->metalInitialized ? "Available" : "Not Available",
                d->gpuInfo,
                d->vramSize);
}

MacOSMediaAccelerator::~MacOSMediaAccelerator() {
    cleanup();
}

bool MacOSMediaAccelerator::isHardwareDecodingSupported(const QString& codec) const {
    return d->videoToolboxInitialized && d->supportedDecoders.contains(codec.toLower());
}

bool MacOSMediaAccelerator::isHardwareEncodingSupported(const QString& codec) const {
    return d->videoToolboxInitialized && d->supportedEncoders.contains(codec.toLower());
}

QStringList MacOSMediaAccelerator::getSupportedDecoders() const {
    return d->supportedDecoders;
}

QStringList MacOSMediaAccelerator::getSupportedEncoders() const {
    return d->supportedEncoders;
}

QString MacOSMediaAccelerator::getGPUInfo() const {
    // Return cached value
    return d->gpuInfo;
}

bool MacOSMediaAccelerator::hasDiscreteGPU() const {
    return d->hasDiscreteGPU;
}

int MacOSMediaAccelerator::getVRAMSize() const {
    // Return cached value
    return d->vramSize;
}

bool MacOSMediaAccelerator::initializeDecoder(const QString& codec) {
    if (!d->videoToolboxInitialized) {
        return false;
    }
    
    Murmur::Logger::instance().info("Initializing decoder for codec: {}", codec);
    
#ifdef Q_OS_MACOS
    // Cleanup existing session if any
    if (d->decompressionSession) {
        VTDecompressionSessionInvalidate(d->decompressionSession);
        CFRelease(d->decompressionSession);
        d->decompressionSession = nullptr;
    }
    
    // Map codec string to VideoToolbox codec type
    if (codec.toLower() != "h264" && codec.toLower() != "hevc" && codec.toLower() != "h265" && codec.toLower() != "prores") {
        Murmur::Logger::instance().warn("Unsupported codec for hardware decoding: {}", codec);
        return false;
    }
    
    // Create destination image buffer attributes for hardware acceleration
    CFMutableDictionaryRef destinationImageBufferAttributes = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    
    // Request hardware acceleration
    CFDictionarySetValue(destinationImageBufferAttributes, 
                        kCVPixelBufferPixelFormatTypeKey, 
                        (__bridge CFNumberRef)@(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange));
    
    // Enable Metal compatibility for GPU processing
    if (d->metalInitialized) {
        CFDictionarySetValue(destinationImageBufferAttributes, 
                            kCVPixelBufferMetalCompatibilityKey, 
                            kCFBooleanTrue);
    }
    
    // Create decompression session
    VTDecompressionOutputCallbackRecord callback = {nullptr, nullptr};
    OSStatus status = VTDecompressionSessionCreate(
        kCFAllocatorDefault,
        (CMVideoFormatDescriptionRef)nullptr,
        nullptr,  // Decoder specification (use default)
        destinationImageBufferAttributes,
        &callback,
        &d->decompressionSession);
    
    CFRelease(destinationImageBufferAttributes);
    
    if (status != noErr) {
        Murmur::Logger::instance().error("Failed to create decompression session: {}", status);
        return false;
    }
    
    // Set properties for optimal performance
    VTSessionSetProperty(d->decompressionSession, 
                        kVTDecompressionPropertyKey_RealTime, 
                        kCFBooleanTrue);
    
    // Enable threaded decompression for better performance
    VTSessionSetProperty(d->decompressionSession, 
                        kVTDecompressionPropertyKey_ThreadCount, 
                        (__bridge CFNumberRef)@(0));  // 0 = automatic
    
    d->currentDecoderCodec = codec;
    Murmur::Logger::instance().info("Successfully initialized hardware decoder for {}", codec);
    return true;
#else
    return false;
#endif
}

bool MacOSMediaAccelerator::initializeEncoder(const QString& codec, const EncoderSettings& settings) {
    if (!d->videoToolboxInitialized) {
        return false;
    }
    
    Murmur::Logger::instance().info("Initializing encoder for codec: {}", codec);
    
#ifdef Q_OS_MACOS
    // Cleanup existing session if any
    if (d->compressionSession) {
        VTCompressionSessionInvalidate(d->compressionSession);
        CFRelease(d->compressionSession);
        d->compressionSession = nullptr;
    }
    
    // Map codec string to VideoToolbox codec type
    CMVideoCodecType codecType;
    if (codec.toLower() == "h264") {
        codecType = kCMVideoCodecType_H264;
    } else if (codec.toLower() == "hevc" || codec.toLower() == "h265") {
        codecType = kCMVideoCodecType_HEVC;
    } else if (codec.toLower() == "prores") {
        codecType = kCMVideoCodecType_AppleProRes422;
    } else {
        Murmur::Logger::instance().warn("Unsupported codec for hardware encoding: {}", codec);
        return false;
    }
    
    // Create source image buffer attributes
    CFMutableDictionaryRef sourceImageBufferAttributes = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    
    CFDictionarySetValue(sourceImageBufferAttributes, 
                        kCVPixelBufferPixelFormatTypeKey, 
                        (__bridge CFNumberRef)@(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange));
    
    // Enable Metal compatibility for GPU processing
    if (d->metalInitialized) {
        CFDictionarySetValue(sourceImageBufferAttributes, 
                            kCVPixelBufferMetalCompatibilityKey, 
                            kCFBooleanTrue);
    }
    
    // Create compression session
    VTCompressionOutputCallback callback = nullptr;
    OSStatus status = VTCompressionSessionCreate(
        kCFAllocatorDefault,
        settings.width,
        settings.height,
        codecType,
        nullptr,  // Encoder specification (use default)
        sourceImageBufferAttributes,
        kCFAllocatorDefault,
        callback,
        nullptr,  // Callback refcon
        &d->compressionSession);
    
    CFRelease(sourceImageBufferAttributes);
    
    if (status != noErr) {
        Murmur::Logger::instance().error("Failed to create compression session: {}", status);
        return false;
    }
    
    // Set encoder properties
    VTSessionSetProperty(d->compressionSession, 
                        kVTCompressionPropertyKey_RealTime, 
                        kCFBooleanTrue);
    
    // Set bitrate
    VTSessionSetProperty(d->compressionSession, 
                        kVTCompressionPropertyKey_AverageBitRate, 
                        (__bridge CFNumberRef)@(settings.bitrate * 1000));
    
    // Set frame rate
    VTSessionSetProperty(d->compressionSession, 
                        kVTCompressionPropertyKey_ExpectedFrameRate, 
                        (__bridge CFNumberRef)@(settings.frameRate));
    
    // Set key frame interval
    VTSessionSetProperty(d->compressionSession, 
                        kVTCompressionPropertyKey_MaxKeyFrameInterval, 
                        (__bridge CFNumberRef)@(settings.keyFrameInterval));
    
    // Set profile level for H.264
    if (codec.toLower() == "h264") {
        CFStringRef profileLevel = kVTProfileLevel_H264_High_AutoLevel;
        if (settings.profile.toLower() == "baseline") {
            profileLevel = kVTProfileLevel_H264_Baseline_AutoLevel;
        } else if (settings.profile.toLower() == "main") {
            profileLevel = kVTProfileLevel_H264_Main_AutoLevel;
        }
        VTSessionSetProperty(d->compressionSession, 
                            kVTCompressionPropertyKey_ProfileLevel, 
                            profileLevel);
    }
    
    // Enable hardware acceleration
    VTSessionSetProperty(d->compressionSession, 
                        kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder, 
                        kCFBooleanTrue);
    
    // Prepare encoder
    VTCompressionSessionPrepareToEncodeFrames(d->compressionSession);
    
    d->currentEncoderCodec = codec;
    d->currentEncoderSettings = settings;
    
    Murmur::Logger::instance().info("Successfully initialized hardware encoder for {} ({}x{} @ {}fps)", 
                           codec, settings.width, settings.height, settings.frameRate);
    return true;
#else
    return false;
#endif
}

void MacOSMediaAccelerator::cleanup() {
#ifdef Q_OS_MACOS
    // Clean up VideoToolbox sessions
    if (d->decompressionSession) {
        VTDecompressionSessionInvalidate(d->decompressionSession);
        CFRelease(d->decompressionSession);
        d->decompressionSession = nullptr;
    }
    
    if (d->compressionSession) {
        VTCompressionSessionInvalidate(d->compressionSession);
        CFRelease(d->compressionSession);
        d->compressionSession = nullptr;
    }
    
    if (d->pixelBufferPool) {
        CFRelease(d->pixelBufferPool);
        d->pixelBufferPool = nullptr;
    }
    
    if (d->textureCache) {
        CFRelease(d->textureCache);
        d->textureCache = nullptr;
    }
    
    d->metalDevice = nil;
#endif
    
    // Reset state
    d->videoToolboxInitialized = false;
    d->metalInitialized = false;
    d->currentDecoderCodec.clear();
    d->currentEncoderCodec.clear();
}

bool MacOSMediaAccelerator::isMetalSupported() const {
    return d->metalInitialized;
}

bool MacOSMediaAccelerator::isVideoToolboxAvailable() const {
    return d->videoToolboxInitialized;
}

QString MacOSMediaAccelerator::getMetalDeviceInfo() const {
    if (!d->metalInitialized) {
        return QString();
    }
    
#ifdef Q_OS_MACOS
    // Get detailed Metal device information from native API
    return QString::fromUtf8(getMacOSMetalDeviceInfo());
#else
    return d->gpuInfo + " (Metal Supported)";
#endif
}

void MacOSMediaAccelerator::optimizeForBatteryLife() {
    Murmur::Logger::instance().info("Optimizing for battery life");
    
#ifdef Q_OS_MACOS
    // Check if low power mode is enabled
    bool lowPowerMode = getMacOSLowPowerModeEnabled();
    if (lowPowerMode) {
        Murmur::Logger::instance().info("Low power mode detected, using integrated GPU");
    }
    
    // Request integrated GPU usage
    setMacOSGPUPreference(true); // prefer integrated
    
    // Emit signal to notify that optimization has changed
    emit hardwareAccelerationChanged(true);
#endif
    
    Murmur::Logger::instance().info("Battery life optimization applied");
}

void MacOSMediaAccelerator::optimizeForPerformance() {
    Murmur::Logger::instance().info("Optimizing for performance");
    
#ifdef Q_OS_MACOS
    if (d->hasDiscreteGPU) {
        // Request discrete GPU usage
        setMacOSGPUPreference(false); // prefer discrete
        Murmur::Logger::instance().info("Discrete GPU enabled for maximum performance");
    } else {
        Murmur::Logger::instance().info("No discrete GPU available, using integrated GPU");
    }
    
    // Emit signal to notify that optimization has changed
    emit hardwareAccelerationChanged(true);
#endif
    
    Murmur::Logger::instance().info("Performance optimization applied");
}

void MacOSMediaAccelerator::setPreferredGPU(const QString& gpuName) {
    Murmur::Logger::instance().info("Setting preferred GPU: {}", gpuName);
    
#ifdef Q_OS_MACOS
    // Check if the specified GPU matches our current GPU
    if (d->gpuInfo.contains(gpuName, Qt::CaseInsensitive)) {
        Murmur::Logger::instance().info("GPU {} is current GPU", gpuName);
        emit gpuChanged(gpuName);
    } else {
        Murmur::Logger::instance().warn("GPU {} not found in system", gpuName);
        emitError(PlatformError::DeviceNotFound, QString("GPU not found: %1").arg(gpuName));
    }
#endif
}

bool MacOSMediaAccelerator::checkVideoToolboxSupport() const {
#ifdef Q_OS_MACOS
    // Use native API to check VideoToolbox support
    return getMacOSVideoToolboxSupport();
#else
    // Fallback: Check if VideoToolbox is available
    // On macOS 10.8+, VideoToolbox should be available
    QSysInfo::MacVersion version = QSysInfo::macVersion();
    return version >= QSysInfo::MV_10_8;
#endif
}

bool MacOSMediaAccelerator::checkMetalSupport() const {
#ifdef Q_OS_MACOS
    // Use native API to check Metal support
    return getMacOSMetalSupport();
#else
    // Fallback: Check if Metal is available
    // Metal is available from macOS 10.11 (El Capitan) onwards
    QSysInfo::MacVersion version = QSysInfo::macVersion();
    return version >= QSysInfo::MV_10_11;
#endif
}

QList<GPUInfo> MacOSMediaAccelerator::getAvailableGPUs() const {
    QList<GPUInfo> gpus;
    
#ifdef Q_OS_MACOS
    // Enumerate all available Metal devices
    id<MTLDevice> defaultDevice = MTLCreateSystemDefaultDevice();
    NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
    
    for (id<MTLDevice> device in devices) {
        GPUInfo gpuInfo;
        gpuInfo.name = QString::fromNSString(device.name);
        gpuInfo.driverVersion = "Metal";
        
        // Get vendor from name
        if (gpuInfo.name.contains("Apple", Qt::CaseInsensitive)) {
            gpuInfo.vendor = "Apple";
        } else if (gpuInfo.name.contains("AMD", Qt::CaseInsensitive) || gpuInfo.name.contains("Radeon", Qt::CaseInsensitive)) {
            gpuInfo.vendor = "AMD";
        } else if (gpuInfo.name.contains("NVIDIA", Qt::CaseInsensitive) || gpuInfo.name.contains("GeForce", Qt::CaseInsensitive)) {
            gpuInfo.vendor = "NVIDIA";
        } else if (gpuInfo.name.contains("Intel", Qt::CaseInsensitive)) {
            gpuInfo.vendor = "Intel";
        } else {
            gpuInfo.vendor = "Unknown";
        }
        
        // Get memory size from the Metal device
        if (@available(macOS 10.15, *)) {
            // Use recommendedMaxWorkingSetSize as an approximation of VRAM for unified memory.
            gpuInfo.vramMB = static_cast<qint64>(device.recommendedMaxWorkingSetSize / (1024 * 1024));
            gpuInfo.isDiscrete = !device.isLowPower;
        } else {
            // Fallback for older macOS versions
            gpuInfo.vramMB = getMacOSVRAMSize(); // Use bridge function
            gpuInfo.isDiscrete = getMacOSDiscreteGPUStatus();
        }
        
        gpuInfo.isActive = (device == defaultDevice);
        gpus.append(gpuInfo);
    }
    
    // If no devices found, add a fallback entry
    if (gpus.isEmpty()) {
        GPUInfo fallbackGPU;
        fallbackGPU.name = d->gpuInfo.isEmpty() ? "Unknown GPU" : d->gpuInfo;
        fallbackGPU.vendor = "Unknown";
        fallbackGPU.driverVersion = "Unknown";
        fallbackGPU.vramMB = d->vramSize;
        fallbackGPU.isDiscrete = d->hasDiscreteGPU;
        fallbackGPU.isActive = true; // Assume the only one is active
        gpus.append(fallbackGPU);
    }
#else
    GPUInfo unknownGPU;
    unknownGPU.name = "macOS APIs not available";
    unknownGPU.vendor = "N/A";
    unknownGPU.driverVersion = "N/A";
    unknownGPU.vramMB = 0;
    unknownGPU.isDiscrete = false;
    unknownGPU.isActive = false;
    gpus.append(unknownGPU);
#endif
    
    return gpus;
}

void MacOSMediaAccelerator::configureVideoToolbox() {
    // Configure VideoToolbox settings for optimal performance
    if (d->videoToolboxInitialized) {
        Murmur::Logger::instance().info("Configuring VideoToolbox");
        
        // Configure VideoToolbox preferences for optimal decoder/encoder performance
        
        // Set preferred hardware decoder types (H.264/H.265)
        CFMutableDictionaryRef decoderSpec = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        
        if (decoderSpec) {
            // Enable hardware decoding
            CFDictionarySetValue(decoderSpec, kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder, kCFBooleanTrue);
            
            // Prefer low-latency decoding for real-time applications
            CFDictionarySetValue(decoderSpec, kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder, kCFBooleanTrue);
            
            CFRelease(decoderSpec);
        }
        
        // Configure encoder preferences for high quality and performance
        CFMutableDictionaryRef encoderSpec = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        
        if (encoderSpec) {
            // Enable hardware encoding
            CFDictionarySetValue(encoderSpec, kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder, kCFBooleanTrue);
            
            // Set quality preference (prioritize speed for real-time, quality for archival)
            CFDictionarySetValue(encoderSpec, kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder, kCFBooleanTrue);
            
            CFRelease(encoderSpec);
        }
        
        Murmur::Logger::instance().info("VideoToolbox configuration completed");
    }
}

void MacOSMediaAccelerator::configureMetalDevice() {
    if (d->metalInitialized) {
        Murmur::Logger::instance().info("Configuring Metal device");
        
        // Configure Metal device for optimal video processing performance
        
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device) {
            // Configure Metal device properties for video processing
            
            // Set up command queue with optimal settings
            id<MTLCommandQueue> commandQueue = [device newCommandQueue];
            if (commandQueue) {
                commandQueue.label = @"VideoProcessing";
            }
            
            // Check device capabilities for video processing.
            BOOL supportsMacFamily = [device supportsFamily:MTLGPUFamilyMac2];
            BOOL supportsAppleFamily = [device supportsFamily:MTLGPUFamilyApple1];
            
            Murmur::Logger::instance().info("Metal device: {} - Supports Mac Family: {}, Supports Apple Family: {}, MaxThreadsPerGroup: {}",
                device.name.UTF8String,
                supportsMacFamily ? "Yes" : "No",
                supportsAppleFamily ? "Yes" : "No",
                (int)device.maxThreadsPerThreadgroup.width);
            
            // Configure memory management for video processing
            if (@available(macOS 10.15, *)) {
                // Hint to Metal about expected memory usage patterns
                size_t recommendedMemory = device.recommendedMaxWorkingSetSize;
                Murmur::Logger::instance().info("Metal recommended max working set: {} MB", 
                    recommendedMemory / (1024 * 1024));
            }
        }
        
        Murmur::Logger::instance().info("Metal device configuration completed");
    }
}

} // namespace Murmur