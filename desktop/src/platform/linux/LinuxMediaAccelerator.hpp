#pragma once

#include "../../core/media/PlatformAccelerator.hpp"
#include <QtCore/QObject>
#include <memory>

namespace Murmur {

/**
 * @brief Linux-specific hardware acceleration using VA-API, VDPAU, and Vulkan
 * 
 * Provides Linux-specific implementations for hardware-accelerated video
 * encoding/decoding using VA-API (Intel), VDPAU (NVIDIA), Vulkan, and OpenGL.
 */
class LinuxMediaAccelerator : public PlatformAccelerator {
    Q_OBJECT

public:
    explicit LinuxMediaAccelerator(QObject* parent = nullptr);
    ~LinuxMediaAccelerator() override;

    // Hardware capability detection
    bool isHardwareDecodingSupported(const QString& codec) const override;
    bool isHardwareEncodingSupported(const QString& codec) const override;
    QStringList getSupportedDecoders() const override;
    QStringList getSupportedEncoders() const override;

    // GPU information
    QString getGPUInfo() const override;
    bool hasDiscreteGPU() const override;
    int getVRAMSize() const override;
    QList<GPUInfo> getAvailableGPUs() const override;

    // Codec initialization
    bool initializeDecoder(const QString& codec) override;
    bool initializeEncoder(const QString& codec, const EncoderSettings& settings) override;
    void cleanup() override;

    // Linux-specific features
    bool isVAAPISupported() const;
    bool isVDPAUSupported() const;
    bool isVulkanSupported() const;
    bool isOpenGLSupported() const;
    QString getVAAPIVersion() const;
    QString getVDPAUVersion() const;
    void setPreferredGPU(const QString& gpuName) override;

    // Power management
    void optimizeForBatteryLife() override;
    void optimizeForPerformance() override;

private:
    struct LinuxMediaAcceleratorPrivate;
    std::unique_ptr<LinuxMediaAcceleratorPrivate> d;

    void initializeVAAPI();
    void initializeVDPAU();
    void initializeVulkan();
    void detectHardwareCapabilities();
    void enumerateGPUs();
    void detectDrivers();
};

} // namespace Murmur