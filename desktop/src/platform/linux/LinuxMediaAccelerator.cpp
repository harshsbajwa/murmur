#include "LinuxMediaAccelerator.hpp"
#include "../../core/common/Logger.hpp"
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QStandardPaths>

#ifdef Q_OS_LINUX
// Linux-specific includes
#include <unistd.h>
#include <sys/utsname.h>
#include <dlfcn.h>

// VA-API includes (if available)
#ifdef HAVE_VAAPI
#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_x11.h>
#endif

// VDPAU includes (if available)
#ifdef HAVE_VDPAU
#include <vdpau/vdpau.h>
#include <vdpau/vdpau_x11.h>
#endif

// Vulkan includes (if available)
#ifdef HAVE_VULKAN
#include <vulkan/vulkan.h>
#endif

// OpenGL includes
#include <GL/gl.h>
#include <GL/glx.h>

// External C functions for Linux-specific operations
extern "C" {
    const char* getLinuxGPUInfo();
    bool getLinuxDiscreteGPUStatus();
    int getLinuxVRAMSize();
    bool getLinuxVAAPISupport();
    bool getLinuxVDPAUSupport();
    const char* getLinuxVAAPIVersion();
    const char* getLinuxVDPAUVersion();
    bool getLinuxVulkanSupport();
    bool getLinuxOpenGLSupport();
    void setLinuxGPUPreference(bool preferIntegrated);
    bool getLinuxPowerSaveMode();
}

#endif

namespace Murmur {

struct LinuxMediaAccelerator::LinuxMediaAcceleratorPrivate {
    bool vaapiInitialized = false;
    bool vdpauInitialized = false;
    bool vulkanInitialized = false;
    bool openglInitialized = false;
    
#ifdef Q_OS_LINUX
    // VA-API context
#ifdef HAVE_VAAPI
    VADisplay vaDisplay = nullptr;
    VAContextID vaContext = VA_INVALID_ID;
    VAConfigID vaConfig = VA_INVALID_ID;
#endif
    
    // VDPAU context
#ifdef HAVE_VDPAU
    VdpDevice vdpDevice = VDP_INVALID_HANDLE;
    VdpGetProcAddress* vdpGetProcAddress = nullptr;
    VdpVideoMixerCreate* vdpVideoMixerCreate = nullptr;
    VdpDecoderCreate* vdpDecoderCreate = nullptr;
#endif
    
    // Vulkan context
#ifdef HAVE_VULKAN
    VkInstance vulkanInstance = VK_NULL_HANDLE;
    VkDevice vulkanDevice = VK_NULL_HANDLE;
    VkPhysicalDevice vulkanPhysicalDevice = VK_NULL_HANDLE;
#endif
    
    // OpenGL context
    Display* x11Display = nullptr;
    GLXContext glxContext = nullptr;
    
    // Current codec settings
    QString currentDecoderCodec;
    QString currentEncoderCodec;
    EncoderSettings currentEncoderSettings;
#endif
    
    QStringList supportedDecoders;
    QStringList supportedEncoders;
    QString gpuInfo;
    QString vaapiVersion;
    QString vdpauVersion;
    QString driverInfo;
    bool hasDiscreteGPU = false;
    int vramSize = 0;
    QList<GPUInfo> availableGPUs;
};

LinuxMediaAccelerator::LinuxMediaAccelerator(QObject* parent)
    : PlatformAccelerator(parent)
    , d(std::make_unique<LinuxMediaAcceleratorPrivate>()) {
    
    Logger::instance().info("Initializing Linux media acceleration with VA-API, VDPAU, and Vulkan");
    
#ifdef Q_OS_LINUX
    // Use external API calls for hardware detection
    d->vaapiInitialized = getLinuxVAAPISupport();
    d->vdpauInitialized = getLinuxVDPAUSupport();
    d->vulkanInitialized = getLinuxVulkanSupport();
    d->openglInitialized = getLinuxOpenGLSupport();
    d->hasDiscreteGPU = getLinuxDiscreteGPUStatus();
    d->vramSize = getLinuxVRAMSize();
    d->gpuInfo = QString::fromUtf8(getLinuxGPUInfo());
    d->vaapiVersion = QString::fromUtf8(getLinuxVAAPIVersion());
    d->vdpauVersion = QString::fromUtf8(getLinuxVDPAUVersion());
    
    if (d->vaapiInitialized) {
        initializeVAAPI();
    }
    
    if (d->vdpauInitialized) {
        initializeVDPAU();
    }
    
    if (d->vulkanInitialized) {
        initializeVulkan();
    }
    
    detectDrivers();
    detectHardwareCapabilities();
    enumerateGPUs();
#else
    // Fallback for non-Linux builds
    d->vaapiInitialized = false;
    d->vdpauInitialized = false;
    d->vulkanInitialized = false;
    d->openglInitialized = false;
    d->hasDiscreteGPU = false;
    d->vramSize = 0;
    d->gpuInfo = "Linux APIs not available";
    d->vaapiVersion = "N/A";
    d->vdpauVersion = "N/A";
    d->driverInfo = "N/A";
#endif
    
    Logger::instance().info("VA-API: {}, VDPAU: {}, Vulkan: {}, OpenGL: {}, GPU: {} ({} MB VRAM)",
                d->vaapiInitialized ? "Available" : "Not Available",
                d->vdpauInitialized ? "Available" : "Not Available",
                d->vulkanInitialized ? "Available" : "Not Available", 
                d->openglInitialized ? "Available" : "Not Available",
                d->gpuInfo,
                d->vramSize);
}

LinuxMediaAccelerator::~LinuxMediaAccelerator() {
    cleanup();
}

bool LinuxMediaAccelerator::isHardwareDecodingSupported(const QString& codec) const {
    return (d->vaapiInitialized || d->vdpauInitialized) && d->supportedDecoders.contains(codec.toLower());
}

bool LinuxMediaAccelerator::isHardwareEncodingSupported(const QString& codec) const {
    return d->vaapiInitialized && d->supportedEncoders.contains(codec.toLower());
}

QStringList LinuxMediaAccelerator::getSupportedDecoders() const {
    return d->supportedDecoders;
}

QStringList LinuxMediaAccelerator::getSupportedEncoders() const {
    return d->supportedEncoders;
}

QString LinuxMediaAccelerator::getGPUInfo() const {
    return d->gpuInfo;
}

bool LinuxMediaAccelerator::hasDiscreteGPU() const {
    return d->hasDiscreteGPU;
}

int LinuxMediaAccelerator::getVRAMSize() const {
    return d->vramSize;
}

bool LinuxMediaAccelerator::initializeDecoder(const QString& codec) {
    if (!d->vaapiInitialized && !d->vdpauInitialized) {
        return false;
    }
    
    Logger::instance().info("Initializing Linux decoder for codec: {}", codec);
    
#ifdef Q_OS_LINUX
    // Prefer VA-API over VDPAU for decoding
    if (d->vaapiInitialized) {
#ifdef HAVE_VAAPI
        // Map codec to VA profile
        VAProfile profile = VAProfileNone;
        if (codec.toLower() == "h264") {
            profile = VAProfileH264Main;
        } else if (codec.toLower() == "hevc" || codec.toLower() == "h265") {
            profile = VAProfileHEVCMain;
        } else if (codec.toLower() == "vp8") {
            profile = VAProfileVP8Version0_3;
        } else if (codec.toLower() == "vp9") {
            profile = VAProfileVP9Profile0;
        } else if (codec.toLower() == "av1") {
            profile = VAProfileAV1Profile0;
        } else {
            Logger::instance().warn("Unsupported codec for VA-API decoding: {}", codec);
            return false;
        }
        
        // Create decoder configuration
        VAConfigAttrib attrib;
        attrib.type = VAConfigAttribRTFormat;
        attrib.value = VA_RT_FORMAT_YUV420;
        
        VAStatus vaStatus = vaCreateConfig(d->vaDisplay, profile, VAEntrypointVLD, 
                                          &attrib, 1, &d->vaConfig);
        if (vaStatus != VA_STATUS_SUCCESS) {
            Logger::instance().error("Failed to create VA-API config: {}", vaStatus);
            return false;
        }
        
        // Create context with default dimensions (will be recreated with actual dimensions later)
        uint32_t defaultWidth = 1920;   // Default HD resolution
        uint32_t defaultHeight = 1080;
        vaStatus = vaCreateContext(d->vaDisplay, d->vaConfig, defaultWidth, defaultHeight, 
                                  VA_PROGRESSIVE, nullptr, 0, &d->vaContext);
        if (vaStatus != VA_STATUS_SUCCESS) {
            Logger::instance().error("Failed to create VA-API context: {}", vaStatus);
            vaDestroyConfig(d->vaDisplay, d->vaConfig);
            d->vaConfig = VA_INVALID_ID;
            return false;
        }
        
        d->currentDecoderCodec = codec;
        Logger::instance().info("Successfully initialized VA-API decoder for {}", codec);
        return true;
#endif
    } else if (d->vdpauInitialized) {
#ifdef HAVE_VDPAU
        // VDPAU decoder initialization
        VdpDecoderProfile vdpProfile;
        if (codec.toLower() == "h264") {
            vdpProfile = VDP_DECODER_PROFILE_H264_MAIN;
        } else if (codec.toLower() == "hevc" || codec.toLower() == "h265") {
            vdpProfile = VDP_DECODER_PROFILE_HEVC_MAIN;
        } else {
            Logger::instance().warn("Unsupported codec for VDPAU decoding: {}", codec);
            return false;
        }
        
        // Create VDPAU decoder with default dimensions
        // Note: Decoder will be recreated with actual dimensions when video stream is analyzed
        VdpDecoder decoder;
        uint32_t defaultWidth = 1920;  // Default HD resolution
        uint32_t defaultHeight = 1080;
        uint32_t maxReferences = 16;   // Sufficient for most H.264/HEVC streams
        
        VdpStatus status = d->vdpDecoderCreate(d->vdpDevice, vdpProfile, 
                                             defaultWidth, defaultHeight, maxReferences, &decoder);
        if (status != VDP_STATUS_OK) {
            Logger::instance().error("Failed to create VDPAU decoder: {}", status);
            return false;
        }
        
        d->currentDecoderCodec = codec;
        Logger::instance().info("Successfully initialized VDPAU decoder for {}", codec);
        return true;
#endif
    }
#endif
    
    return false;
}

bool LinuxMediaAccelerator::initializeEncoder(const QString& codec, const EncoderSettings& settings) {
    if (!d->vaapiInitialized) {
        return false; 
        // Currently only VA-API supports encoding
    }
    
    Logger::instance().info("Initializing Linux encoder for codec: {}", codec);
    
#ifdef Q_OS_LINUX
#ifdef HAVE_VAAPI
    // Map codec to VA profile
    VAProfile profile = VAProfileNone;
    if (codec.toLower() == "h264") {
        profile = VAProfileH264Main;
    } else if (codec.toLower() == "hevc" || codec.toLower() == "h265") {
        profile = VAProfileHEVCMain;
    } else {
        Logger::instance().warn("Unsupported codec for VA-API encoding: {}", codec);
        return false;
    }
    
    // Create encoder configuration
    VAConfigAttrib attribs[2];
    attribs[0].type = VAConfigAttribRTFormat;
    attribs[0].value = VA_RT_FORMAT_YUV420;
    attribs[1].type = VAConfigAttribRateControl;
    attribs[1].value = VA_RC_CBR; // Constant bitrate
    
    VAConfigID encConfig;
    VAStatus vaStatus = vaCreateConfig(d->vaDisplay, profile, VAEntrypointEncSlice, 
                                      attribs, 2, &encConfig);
    if (vaStatus != VA_STATUS_SUCCESS) {
        Logger::instance().error("Failed to create VA-API encoder config: {}", vaStatus);
        return false;
    }
    
    // Create encoding context
    VAContextID encContext;
    vaStatus = vaCreateContext(d->vaDisplay, encConfig, settings.width, settings.height, 
                              VA_PROGRESSIVE, nullptr, 0, &encContext);
    if (vaStatus != VA_STATUS_SUCCESS) {
        Logger::instance().error("Failed to create VA-API encoder context: {}", vaStatus);
        vaDestroyConfig(d->vaDisplay, encConfig);
        return false;
    }
    
    d->currentEncoderCodec = codec;
    d->currentEncoderSettings = settings;
    
    Logger::instance().info("Successfully initialized VA-API encoder for {} ({}x{} @ {}fps)", 
                           codec, settings.width, settings.height, settings.frameRate);
    return true;
#endif
#endif
    
    return false;
}

void LinuxMediaAccelerator::cleanup() {
#ifdef Q_OS_LINUX
    // Clean up VA-API
#ifdef HAVE_VAAPI
    if (d->vaContext != VA_INVALID_ID) {
        vaDestroyContext(d->vaDisplay, d->vaContext);
        d->vaContext = VA_INVALID_ID;
    }
    
    if (d->vaConfig != VA_INVALID_ID) {
        vaDestroyConfig(d->vaDisplay, d->vaConfig);
        d->vaConfig = VA_INVALID_ID;
    }
    
    if (d->vaDisplay) {
        vaTerminate(d->vaDisplay);
        d->vaDisplay = nullptr;
    }
#endif
    
    // Clean up VDPAU
#ifdef HAVE_VDPAU
    if (d->vdpDevice != VDP_INVALID_HANDLE) {
        // VDPAU cleanup would go here
        d->vdpDevice = VDP_INVALID_HANDLE;
    }
#endif
    
    // Clean up Vulkan
#ifdef HAVE_VULKAN
    if (d->vulkanDevice != VK_NULL_HANDLE) {
        vkDestroyDevice(d->vulkanDevice, nullptr);
        d->vulkanDevice = VK_NULL_HANDLE;
    }
    
    if (d->vulkanInstance != VK_NULL_HANDLE) {
        vkDestroyInstance(d->vulkanInstance, nullptr);
        d->vulkanInstance = VK_NULL_HANDLE;
    }
#endif
    
    // Clean up OpenGL
    if (d->glxContext) {
        glXDestroyContext(d->x11Display, d->glxContext);
        d->glxContext = nullptr;
    }
    
    if (d->x11Display) {
        XCloseDisplay(d->x11Display);
        d->x11Display = nullptr;
    }
#endif
    
    // Reset state
    d->vaapiInitialized = false;
    d->vdpauInitialized = false;
    d->vulkanInitialized = false;
    d->openglInitialized = false;
    d->currentDecoderCodec.clear();
    d->currentEncoderCodec.clear();
}

bool LinuxMediaAccelerator::isVAAPISupported() const {
    return d->vaapiInitialized;
}

bool LinuxMediaAccelerator::isVDPAUSupported() const {
    return d->vdpauInitialized;
}

bool LinuxMediaAccelerator::isVulkanSupported() const {
    return d->vulkanInitialized;
}

bool LinuxMediaAccelerator::isOpenGLSupported() const {
    return d->openglInitialized;
}

QString LinuxMediaAccelerator::getVAAPIVersion() const {
    return d->vaapiVersion;
}

QString LinuxMediaAccelerator::getVDPAUVersion() const {
    return d->vdpauVersion;
}

void LinuxMediaAccelerator::setPreferredGPU(const QString& gpuName) {
    Logger::instance().info("Setting preferred GPU: {}", gpuName);
    
#ifdef Q_OS_LINUX
    // Find GPU in available list
    for (const auto& gpu : d->availableGPUs) {
        if (gpu.name.contains(gpuName, Qt::CaseInsensitive)) {
            Logger::instance().info("Found GPU: {}", gpu.name);
            emit gpuChanged(gpu.name);
            return;
        }
    }
    
    Logger::instance().warn("GPU {} not found in system", gpuName);
    emitError(PlatformError::DeviceNotFound, QString("GPU not found: %1").arg(gpuName));
#endif
}

void LinuxMediaAccelerator::optimizeForBatteryLife() {
    Logger::instance().info("Optimizing for battery life");
    
#ifdef Q_OS_LINUX
    // Check power save mode
    bool powerSaveMode = getLinuxPowerSaveMode();
    if (powerSaveMode) {
        Logger::instance().info("Power save mode detected, using integrated GPU");
    }
    
    // Request integrated GPU usage
    setLinuxGPUPreference(true); // prefer integrated
    
    emit hardwareAccelerationChanged(true);
#endif
    
    Logger::instance().info("Battery life optimization applied");
}

void LinuxMediaAccelerator::optimizeForPerformance() {
    Logger::instance().info("Optimizing for performance");
    
#ifdef Q_OS_LINUX
    if (d->hasDiscreteGPU) {
        // Request discrete GPU usage
        setLinuxGPUPreference(false); // prefer discrete
        Logger::instance().info("Discrete GPU enabled for maximum performance");
    } else {
        Logger::instance().info("No discrete GPU available, using integrated GPU");
    }
    
    emit hardwareAccelerationChanged(true);
#endif
    
    Logger::instance().info("Performance optimization applied");
}

QList<GPUInfo> LinuxMediaAccelerator::getAvailableGPUs() const {
    return d->availableGPUs;
}

void LinuxMediaAccelerator::initializeVAAPI() {
#ifdef Q_OS_LINUX
#ifdef HAVE_VAAPI
    Logger::instance().info("Initializing VA-API");
    
    // Try to open VA-API display (DRM first, then X11)
    int drmFd = open("/dev/dri/renderD128", O_RDWR);
    if (drmFd >= 0) {
        d->vaDisplay = vaGetDisplayDRM(drmFd);
        Logger::instance().info("Using VA-API DRM display");
    } else {
        // Fallback to X11 display
        Display* x11Display = XOpenDisplay(nullptr);
        if (x11Display) {
            d->vaDisplay = vaGetDisplay(x11Display);
            Logger::instance().info("Using VA-API X11 display");
        }
    }
    
    if (!d->vaDisplay) {
        Logger::instance().error("Failed to get VA-API display");
        return;
    }
    
    // Initialize VA-API
    int majorVersion, minorVersion;
    VAStatus vaStatus = vaInitialize(d->vaDisplay, &majorVersion, &minorVersion);
    if (vaStatus != VA_STATUS_SUCCESS) {
        Logger::instance().error("Failed to initialize VA-API: {}", vaStatus);
        d->vaDisplay = nullptr;
        return;
    }
    
    d->vaapiVersion = QString("VA-API %1.%2").arg(majorVersion).arg(minorVersion);
    Logger::instance().info("VA-API initialized successfully: {}", d->vaapiVersion);
#endif
#endif
}

void LinuxMediaAccelerator::initializeVDPAU() {
#ifdef Q_OS_LINUX
#ifdef HAVE_VDPAU
    Logger::instance().info("Initializing VDPAU");
    
    // Open X11 display for VDPAU
    Display* x11Display = XOpenDisplay(nullptr);
    if (!x11Display) {
        Logger::instance().error("Failed to open X11 display for VDPAU");
        return;
    }
    
    // Create VDPAU device
    VdpStatus status = vdp_device_create_x11(x11Display, DefaultScreen(x11Display), 
                                            &d->vdpDevice, &d->vdpGetProcAddress);
    if (status != VDP_STATUS_OK) {
        Logger::instance().error("Failed to create VDPAU device: {}", status);
        XCloseDisplay(x11Display);
        return;
    }
    
    // Get VDPAU function pointers
    status = d->vdpGetProcAddress(d->vdpDevice, VDP_FUNC_ID_VIDEO_MIXER_CREATE,
                                 reinterpret_cast<void**>(&d->vdpVideoMixerCreate));
    if (status == VDP_STATUS_OK) {
        status = d->vdpGetProcAddress(d->vdpDevice, VDP_FUNC_ID_DECODER_CREATE,
                                     reinterpret_cast<void**>(&d->vdpDecoderCreate));
    }
    
    if (status != VDP_STATUS_OK) {
        Logger::instance().error("Failed to get VDPAU function pointers: {}", status);
        return;
    }
    
    Logger::instance().info("VDPAU initialized successfully");
#endif
#endif
}

void LinuxMediaAccelerator::initializeVulkan() {
#ifdef Q_OS_LINUX
#ifdef HAVE_VULKAN
    Logger::instance().info("Initializing Vulkan");
    
    // Create Vulkan instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Murmur";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Murmur Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;
    
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    VkResult result = vkCreateInstance(&createInfo, nullptr, &d->vulkanInstance);
    if (result != VK_SUCCESS) {
        Logger::instance().error("Failed to create Vulkan instance: {}", result);
        return;
    }
    
    // Enumerate physical devices
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(d->vulkanInstance, &deviceCount, nullptr);
    
    if (deviceCount > 0) {
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(d->vulkanInstance, &deviceCount, devices.data());
        d->vulkanPhysicalDevice = devices[0]; // Use first device
        
        Logger::instance().info("Vulkan initialized with {} devices", deviceCount);
    } else {
        Logger::instance().warn("No Vulkan devices found");
    }
#endif
#endif
}

void LinuxMediaAccelerator::detectHardwareCapabilities() {
#ifdef Q_OS_LINUX
    Logger::instance().info("Detecting hardware acceleration capabilities");
    
    // VA-API capabilities
    if (d->vaapiInitialized) {
        // Common VA-API supported codecs
        d->supportedDecoders.append({"h264", "hevc", "vp8", "vp9", "av1", "mpeg2", "mpeg4"});
        d->supportedEncoders.append({"h264", "hevc"}); // Encoding support varies by hardware
    }
    
    // VDPAU capabilities (mainly NVIDIA)
    if (d->vdpauInitialized && !d->vaapiInitialized) {
        // VDPAU typically supports these codecs
        d->supportedDecoders.append({"h264", "hevc", "mpeg2", "mpeg4", "vc1"});
        // VDPAU doesn't typically support encoding
    }
    
    Logger::instance().info("Detected {} decoders and {} encoders", 
                           d->supportedDecoders.size(), d->supportedEncoders.size());
#endif
}

void LinuxMediaAccelerator::enumerateGPUs() {
#ifdef Q_OS_LINUX
    Logger::instance().info("Enumerating available GPUs");
    
    d->availableGPUs.clear();
    
    // Parse /proc/driver/nvidia/gpus for NVIDIA GPUs
    QDir nvidiaDir("/proc/driver/nvidia/gpus");
    if (nvidiaDir.exists()) {
        QStringList gpuDirs = nvidiaDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& gpuDir : gpuDirs) {
            QFile infoFile(QString("/proc/driver/nvidia/gpus/%1/information").arg(gpuDir));
            if (infoFile.open(QIODevice::ReadOnly)) {
                QString content = infoFile.readAll();
                
                GPUInfo gpuInfo;
                QRegularExpression nameRegex(R"(Model:\s+(.+))");
                QRegularExpressionMatch match = nameRegex.match(content);
                if (match.hasMatch()) {
                    gpuInfo.name = match.captured(1).trimmed();
                } else {
                    gpuInfo.name = "NVIDIA GPU";
                }
                
                gpuInfo.driverVersion = "NVIDIA";
                gpuInfo.memoryMB = 0; // Would need additional detection
                gpuInfo.isActive = true;
                
                d->availableGPUs.append(gpuInfo);
            }
        }
    }
    
    // Parse lspci for additional GPU information
    QProcess lspci;
    lspci.start("lspci", QStringList() << "-nn");
    if (lspci.waitForFinished()) {
        QString output = lspci.readAllStandardOutput();
        QStringList lines = output.split('\n');
        
        for (const QString& line : lines) {
            if (line.contains("VGA") || line.contains("3D controller")) {
                QRegularExpression gpuRegex(R"(([^:]+):\s*(.+))");
                QRegularExpressionMatch match = gpuRegex.match(line);
                if (match.hasMatch()) {
                    QString deviceInfo = match.captured(2);
                    
                    // Skip if we already have this GPU from NVIDIA proc
                    bool alreadyExists = false;
                    for (const auto& existingGpu : d->availableGPUs) {
                        if (deviceInfo.contains(existingGpu.name, Qt::CaseInsensitive)) {
                            alreadyExists = true;
                            break;
                        }
                    }
                    
                    if (!alreadyExists) {
                        GPUInfo gpuInfo;
                        gpuInfo.name = deviceInfo;
                        
                        if (deviceInfo.contains("Intel", Qt::CaseInsensitive)) {
                            gpuInfo.driverVersion = "Intel";
                        } else if (deviceInfo.contains("AMD", Qt::CaseInsensitive) || 
                                  deviceInfo.contains("Radeon", Qt::CaseInsensitive)) {
                            gpuInfo.driverVersion = "AMD";
                        } else {
                            gpuInfo.driverVersion = "Unknown";
                        }
                        
                        gpuInfo.memoryMB = 0; // Would need additional detection
                        gpuInfo.isActive = false;
                        
                        d->availableGPUs.append(gpuInfo);
                    }
                }
            }
        }
    }
    
    // Fallback if no GPUs detected
    if (d->availableGPUs.isEmpty()) {
        GPUInfo fallbackGPU;
        fallbackGPU.name = d->gpuInfo.isEmpty() ? "Unknown GPU" : d->gpuInfo;
        fallbackGPU.driverVersion = "Unknown";
        fallbackGPU.memoryMB = d->vramSize;
        fallbackGPU.isActive = false;
        d->availableGPUs.append(fallbackGPU);
    }
    
    for (const auto& gpu : d->availableGPUs) {
        Logger::instance().info("Found GPU: {} ({})", gpu.name, gpu.driverVersion);
    }
#endif
}

void LinuxMediaAccelerator::detectDrivers() {
#ifdef Q_OS_LINUX
    Logger::instance().info("Detecting graphics drivers");
    
    QStringList driverInfo;
    
    // Check loaded kernel modules
    QFile modulesFile("/proc/modules");
    if (modulesFile.open(QIODevice::ReadOnly)) {
        QString content = modulesFile.readAll();
        
        if (content.contains("nvidia")) {
            driverInfo.append("NVIDIA proprietary driver");
        }
        if (content.contains("amdgpu")) {
            driverInfo.append("AMD open-source driver");
        }
        if (content.contains("radeon")) {
            driverInfo.append("AMD legacy driver");
        }
        if (content.contains("i915")) {
            driverInfo.append("Intel driver");
        }
        if (content.contains("nouveau")) {
            driverInfo.append("Nouveau open-source driver");
        }
    }
    
    d->driverInfo = driverInfo.join(", ");
    if (d->driverInfo.isEmpty()) {
        d->driverInfo = "Unknown drivers";
    }
    
    Logger::instance().info("Detected drivers: {}", d->driverInfo);
#endif
}

} // namespace Murmur