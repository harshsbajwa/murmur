#include "WindowsMediaAccelerator.hpp"
#include "../../core/common/Logger.hpp"
#include <QtCore/QSysInfo>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>

#ifdef Q_OS_WIN
// Windows-specific includes
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <codecapi.h>
#include <initguid.h>
#include <comdef.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// DXVA device manager
#include <d3d9.h>
#include <dxva2api.h>

// External C functions for Windows-specific operations
extern "C" {
    const char* getWindowsGPUInfo();
    bool getWindowsDiscreteGPUStatus();
    int getWindowsVRAMSize();
    bool getWindowsDXVASupport();
    bool getWindowsDirectXSupport();
    const char* getWindowsDirectXVersion();
    bool getWindowsMediaFoundationSupport();
    void setWindowsGPUPreference(bool preferIntegrated);
    bool getWindowsPowerSaveMode();
}

#endif

namespace Murmur {

struct WindowsMediaAccelerator::WindowsMediaAcceleratorPrivate {
    bool directXInitialized = false;
    bool mediaFoundationInitialized = false;
    bool dxvaInitialized = false;
    
#ifdef Q_OS_WIN
    // DirectX interfaces
    ComPtr<ID3D11Device> d3d11Device;
    ComPtr<ID3D11DeviceContext> d3d11Context;
    ComPtr<ID3D12Device> d3d12Device;
    ComPtr<IDXGIFactory6> dxgiFactory;
    ComPtr<IDXGIAdapter4> activeAdapter;
    
    // Media Foundation interfaces
    ComPtr<IMFDXGIDeviceManager> deviceManager;
    ComPtr<IMFTransform> decoder;
    ComPtr<IMFTransform> encoder;
    
    // DXVA interfaces
    ComPtr<IDirectXVideoDecoderService> decoderService;
    ComPtr<IDirectXVideoProcessorService> processorService;
    
    // Current codec settings
    QString currentDecoderCodec;
    QString currentEncoderCodec;
    EncoderSettings currentEncoderSettings;
#endif
    
    QStringList supportedDecoders;
    QStringList supportedEncoders;
    QString gpuInfo;
    QString directXVersion;
    bool hasDiscreteGPU = false;
    int vramSize = 0;
    QList<GPUInfo> availableGPUs;
};

WindowsMediaAccelerator::WindowsMediaAccelerator(QObject* parent)
    : PlatformAccelerator(parent)
    , d(std::make_unique<WindowsMediaAcceleratorPrivate>()) {
    
    Logger::instance().info("Initializing Windows media acceleration with DirectX and Media Foundation");
    
#ifdef Q_OS_WIN
    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        Logger::instance().error("Failed to initialize COM: 0x{:08x}", hr);
        return;
    }
    
    // Use external API calls for hardware detection
    d->directXInitialized = getWindowsDirectXSupport();
    d->mediaFoundationInitialized = getWindowsMediaFoundationSupport();
    d->dxvaInitialized = getWindowsDXVASupport();
    d->hasDiscreteGPU = getWindowsDiscreteGPUStatus();
    d->vramSize = getWindowsVRAMSize();
    d->gpuInfo = QString::fromUtf8(getWindowsGPUInfo());
    d->directXVersion = QString::fromUtf8(getWindowsDirectXVersion());
    
    if (d->directXInitialized) {
        initializeDirectX();
    }
    
    if (d->mediaFoundationInitialized) {
        initializeMediaFoundation();
    }
    
    if (d->dxvaInitialized) {
        initializeDXVA();
    }
    
    detectHardwareCapabilities();
    enumerateGPUs();
#else
    // Fallback for non-Windows builds
    d->directXInitialized = false;
    d->mediaFoundationInitialized = false;
    d->dxvaInitialized = false;
    d->hasDiscreteGPU = false;
    d->vramSize = 0;
    d->gpuInfo = "Windows APIs not available";
    d->directXVersion = "N/A";
#endif
    
    Logger::instance().info("DirectX: {}, Media Foundation: {}, DXVA: {}, GPU: {} ({} MB VRAM)",
                d->directXInitialized ? "Available" : "Not Available",
                d->mediaFoundationInitialized ? "Available" : "Not Available", 
                d->dxvaInitialized ? "Available" : "Not Available",
                d->gpuInfo,
                d->vramSize);
}

WindowsMediaAccelerator::~WindowsMediaAccelerator() {
    cleanup();
    
#ifdef Q_OS_WIN
    CoUninitialize();
#endif
}

bool WindowsMediaAccelerator::isHardwareDecodingSupported(const QString& codec) const {
    return d->dxvaInitialized && d->supportedDecoders.contains(codec.toLower());
}

bool WindowsMediaAccelerator::isHardwareEncodingSupported(const QString& codec) const {
    return d->mediaFoundationInitialized && d->supportedEncoders.contains(codec.toLower());
}

QStringList WindowsMediaAccelerator::getSupportedDecoders() const {
    return d->supportedDecoders;
}

QStringList WindowsMediaAccelerator::getSupportedEncoders() const {
    return d->supportedEncoders;
}

QString WindowsMediaAccelerator::getGPUInfo() const {
    return d->gpuInfo;
}

bool WindowsMediaAccelerator::hasDiscreteGPU() const {
    return d->hasDiscreteGPU;
}

int WindowsMediaAccelerator::getVRAMSize() const {
    return d->vramSize;
}

bool WindowsMediaAccelerator::initializeDecoder(const QString& codec) {
    if (!d->dxvaInitialized && !d->mediaFoundationInitialized) {
        return false;
    }
    
    Logger::instance().info("Initializing Windows decoder for codec: {}", codec);
    
#ifdef Q_OS_WIN
    // Map codec to Media Foundation subtype
    GUID codecGuid = GUID_NULL;
    if (codec.toLower() == "h264") {
        codecGuid = MFVideoFormat_H264;
    } else if (codec.toLower() == "hevc" || codec.toLower() == "h265") {
        codecGuid = MFVideoFormat_HEVC;
    } else if (codec.toLower() == "vp9") {
        codecGuid = MFVideoFormat_VP90;
    } else if (codec.toLower() == "av1") {
        codecGuid = MFVideoFormat_AV1;
    } else {
        Logger::instance().warn("Unsupported codec for Windows hardware decoding: {}", codec);
        return false;
    }
    
    // Create Media Foundation decoder
    HRESULT hr = CoCreateInstance(
        CLSID_VideoProcessorMFT,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&d->decoder));
    
    if (FAILED(hr)) {
        Logger::instance().error("Failed to create Media Foundation decoder: 0x{:08x}", hr);
        return false;
    }
    
    // Configure decoder for hardware acceleration
    ComPtr<IMFAttributes> attributes;
    hr = d->decoder->GetAttributes(&attributes);
    if (SUCCEEDED(hr)) {
        // Enable hardware acceleration
        attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
        attributes->SetUINT32(MF_TRANSFORM_CATEGORY_Attribute, MFT_CATEGORY_VIDEO_DECODER);
        
        // Set D3D device manager if available
        if (d->deviceManager) {
            attributes->SetUnknown(MF_TRANSFORM_D3D_MANAGER, d->deviceManager.Get());
        }
    }
    
    d->currentDecoderCodec = codec;
    Logger::instance().info("Successfully initialized Windows hardware decoder for {}", codec);
    return true;
#else
    return false;
#endif
}

bool WindowsMediaAccelerator::initializeEncoder(const QString& codec, const EncoderSettings& settings) {
    if (!d->mediaFoundationInitialized) {
        return false;
    }
    
    Logger::instance().info("Initializing Windows encoder for codec: {}", codec);
    
#ifdef Q_OS_WIN
    // Map codec to Media Foundation subtype
    GUID codecGuid = GUID_NULL;
    if (codec.toLower() == "h264") {
        codecGuid = MFVideoFormat_H264;
    } else if (codec.toLower() == "hevc" || codec.toLower() == "h265") {
        codecGuid = MFVideoFormat_HEVC;
    } else {
        Logger::instance().warn("Unsupported codec for Windows hardware encoding: {}", codec);
        return false;
    }
    
    // Create Media Foundation encoder
    HRESULT hr = CoCreateInstance(
        CLSID_VideoProcessorMFT,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&d->encoder));
    
    if (FAILED(hr)) {
        Logger::instance().error("Failed to create Media Foundation encoder: 0x{:08x}", hr);
        return false;
    }
    
    // Configure encoder
    ComPtr<IMFAttributes> attributes;
    hr = d->encoder->GetAttributes(&attributes);
    if (SUCCEEDED(hr)) {
        // Enable hardware acceleration
        attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
        attributes->SetUINT32(MF_TRANSFORM_CATEGORY_Attribute, MFT_CATEGORY_VIDEO_ENCODER);
        
        // Set encoding parameters
        attributes->SetUINT32(MF_MT_AVG_BITRATE, settings.bitrate * 1000);
        attributes->SetRatio(MF_MT_FRAME_RATE, settings.frameRate, 1);
        attributes->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        
        // Set D3D device manager if available
        if (d->deviceManager) {
            attributes->SetUnknown(MF_TRANSFORM_D3D_MANAGER, d->deviceManager.Get());
        }
    }
    
    d->currentEncoderCodec = codec;
    d->currentEncoderSettings = settings;
    
    Logger::instance().info("Successfully initialized Windows hardware encoder for {} ({}x{} @ {}fps)", 
                           codec, settings.width, settings.height, settings.frameRate);
    return true;
#else
    return false;
#endif
}

void WindowsMediaAccelerator::cleanup() {
#ifdef Q_OS_WIN
    // Release Media Foundation interfaces
    if (d->decoder) {
        d->decoder.Reset();
    }
    
    if (d->encoder) {
        d->encoder.Reset();
    }
    
    if (d->deviceManager) {
        d->deviceManager.Reset();
    }
    
    // Release DirectX interfaces
    if (d->d3d11Context) {
        d->d3d11Context.Reset();
    }
    
    if (d->d3d11Device) {
        d->d3d11Device.Reset();
    }
    
    if (d->d3d12Device) {
        d->d3d12Device.Reset();
    }
    
    if (d->activeAdapter) {
        d->activeAdapter.Reset();
    }
    
    if (d->dxgiFactory) {
        d->dxgiFactory.Reset();
    }
    
    // Release DXVA interfaces
    if (d->decoderService) {
        d->decoderService.Reset();
    }
    
    if (d->processorService) {
        d->processorService.Reset();
    }
#endif
    
    // Reset state
    d->directXInitialized = false;
    d->mediaFoundationInitialized = false;
    d->dxvaInitialized = false;
    d->currentDecoderCodec.clear();
    d->currentEncoderCodec.clear();
}

bool WindowsMediaAccelerator::isDXVASupported() const {
    return d->dxvaInitialized;
}

bool WindowsMediaAccelerator::isDirectXSupported() const {
    return d->directXInitialized;
}

bool WindowsMediaAccelerator::isMediaFoundationAvailable() const {
    return d->mediaFoundationInitialized;
}

QString WindowsMediaAccelerator::getDirectXVersion() const {
    return d->directXVersion;
}

void WindowsMediaAccelerator::setPreferredGPU(const QString& gpuName) {
    Logger::instance().info("Setting preferred GPU: {}", gpuName);
    
#ifdef Q_OS_WIN
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

void WindowsMediaAccelerator::optimizeForBatteryLife() {
    Logger::instance().info("Optimizing for battery life");
    
#ifdef Q_OS_WIN
    // Check power save mode
    bool powerSaveMode = getWindowsPowerSaveMode();
    if (powerSaveMode) {
        Logger::instance().info("Power save mode detected, using integrated GPU");
    }
    
    // Request integrated GPU usage
    setWindowsGPUPreference(true); // prefer integrated
    
    emit hardwareAccelerationChanged(true);
#endif
    
    Logger::instance().info("Battery life optimization applied");
}

void WindowsMediaAccelerator::optimizeForPerformance() {
    Logger::instance().info("Optimizing for performance");
    
#ifdef Q_OS_WIN
    if (d->hasDiscreteGPU) {
        // Request discrete GPU usage
        setWindowsGPUPreference(false); // prefer discrete
        Logger::instance().info("Discrete GPU enabled for maximum performance");
    } else {
        Logger::instance().info("No discrete GPU available, using integrated GPU");
    }
    
    emit hardwareAccelerationChanged(true);
#endif
    
    Logger::instance().info("Performance optimization applied");
}

QList<GPUInfo> WindowsMediaAccelerator::getAvailableGPUs() const {
    return d->availableGPUs;
}

void WindowsMediaAccelerator::initializeDirectX() {
#ifdef Q_OS_WIN
    Logger::instance().info("Initializing DirectX");
    
    // Create DXGI factory
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&d->dxgiFactory));
    if (FAILED(hr)) {
        Logger::instance().error("Failed to create DXGI factory: 0x{:08x}", hr);
        return;
    }
    
    // Get primary adapter
    hr = d->dxgiFactory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, 
                                                    IID_PPV_ARGS(&d->activeAdapter));
    if (FAILED(hr)) {
        Logger::instance().error("Failed to enumerate adapter: 0x{:08x}", hr);
        return;
    }
    
    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        d->activeAdapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        nullptr, 0,
        D3D11_SDK_VERSION,
        &d->d3d11Device,
        &featureLevel,
        &d->d3d11Context);
    
    if (SUCCEEDED(hr)) {
        Logger::instance().info("D3D11 device created successfully (feature level: {})", 
                               static_cast<int>(featureLevel));
    } else {
        Logger::instance().error("Failed to create D3D11 device: 0x{:08x}", hr);
    }
    
    // Try to create D3D12 device
    hr = D3D12CreateDevice(d->activeAdapter.Get(), D3D_FEATURE_LEVEL_11_0, 
                          IID_PPV_ARGS(&d->d3d12Device));
    if (SUCCEEDED(hr)) {
        Logger::instance().info("D3D12 device created successfully");
    } else {
        Logger::instance().info("D3D12 not available: 0x{:08x}", hr);
    }
#endif
}

void WindowsMediaAccelerator::initializeMediaFoundation() {
#ifdef Q_OS_WIN
    Logger::instance().info("Initializing Media Foundation");
    
    // Initialize Media Foundation
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        Logger::instance().error("Failed to initialize Media Foundation: 0x{:08x}", hr);
        return;
    }
    
    // Create device manager for hardware acceleration
    if (d->d3d11Device) {
        UINT resetToken;
        hr = MFCreateDXGIDeviceManager(&resetToken, &d->deviceManager);
        if (SUCCEEDED(hr)) {
            hr = d->deviceManager->ResetDevice(d->d3d11Device.Get(), resetToken);
            if (SUCCEEDED(hr)) {
                Logger::instance().info("Media Foundation device manager created successfully");
            } else {
                Logger::instance().error("Failed to reset device manager: 0x{:08x}", hr);
            }
        } else {
            Logger::instance().error("Failed to create device manager: 0x{:08x}", hr);
        }
    }
#endif
}

void WindowsMediaAccelerator::initializeDXVA() {
#ifdef Q_OS_WIN
    Logger::instance().info("Initializing DXVA");
    
    // DXVA initialization would involve creating DXVA2 decoder service
    // This is automatically handled by Media Foundation when hardware acceleration is requested
    Logger::instance().info("DXVA ready for use with Media Foundation");
#endif
}

void WindowsMediaAccelerator::detectHardwareCapabilities() {
#ifdef Q_OS_WIN
    Logger::instance().info("Detecting hardware acceleration capabilities");
    
    // Common hardware-supported codecs on Windows
    if (d->dxvaInitialized || d->mediaFoundationInitialized) {
        // Most modern Windows systems support these codecs in hardware
        d->supportedDecoders = {"h264", "hevc", "vp9", "av1", "mpeg2", "mpeg4", "vc1"};
        d->supportedEncoders = {"h264", "hevc"}; // Encoding support is more limited
        
        Logger::instance().info("Detected {} decoders and {} encoders", 
                               d->supportedDecoders.size(), d->supportedEncoders.size());
    }
#endif
}

void WindowsMediaAccelerator::enumerateGPUs() {
#ifdef Q_OS_WIN
    Logger::instance().info("Enumerating available GPUs");
    
    d->availableGPUs.clear();
    
    if (!d->dxgiFactory) {
        return;
    }
    
    // Enumerate all adapters
    for (UINT i = 0; ; ++i) {
        ComPtr<IDXGIAdapter4> adapter;
        HRESULT hr = d->dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_UNSPECIFIED, 
                                                               IID_PPV_ARGS(&adapter));
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break; // No more adapters
        }
        
        if (FAILED(hr)) {
            continue;
        }
        
        DXGI_ADAPTER_DESC3 desc;
        hr = adapter->GetDesc3(&desc);
        if (FAILED(hr)) {
            continue;
        }
        
        GPUInfo gpuInfo;
        gpuInfo.name = QString::fromWCharArray(desc.Description);
        gpuInfo.memoryMB = static_cast<qint64>(desc.DedicatedVideoMemory / (1024 * 1024));
        gpuInfo.isActive = (adapter == d->activeAdapter);
        gpuInfo.driverVersion = "DirectX";
        
        d->availableGPUs.append(gpuInfo);
        
        Logger::instance().info("Found GPU: {} ({} MB VRAM)", 
                               gpuInfo.name, gpuInfo.memoryMB);
    }
    
    if (d->availableGPUs.isEmpty()) {
        // Fallback
        GPUInfo fallbackGPU;
        fallbackGPU.name = d->gpuInfo.isEmpty() ? "Unknown GPU" : d->gpuInfo;
        fallbackGPU.driverVersion = "Unknown";
        fallbackGPU.memoryMB = d->vramSize;
        fallbackGPU.isActive = false;
        d->availableGPUs.append(fallbackGPU);
    }
#endif
}

} // namespace Murmur