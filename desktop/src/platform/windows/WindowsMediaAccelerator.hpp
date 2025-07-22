#pragma once

#include "../../core/media/PlatformAccelerator.hpp"
#include <QtCore/QObject>
#include <memory>

namespace Murmur {

/**
 * @brief Windows-specific hardware acceleration using DirectX and Media Foundation
 * 
 * Provides Windows-specific implementations for hardware-accelerated video
 * encoding/decoding using DirectX 11/12, DXVA, and Media Foundation.
 */
class WindowsMediaAccelerator : public PlatformAccelerator {
    Q_OBJECT

public:
    explicit WindowsMediaAccelerator(QObject* parent = nullptr);
    ~WindowsMediaAccelerator() override;

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

    // Windows-specific features
    bool isDXVASupported() const;
    bool isDirectXSupported() const;
    bool isMediaFoundationAvailable() const;
    QString getDirectXVersion() const;
    void setPreferredGPU(const QString& gpuName) override;

    // Power management
    void optimizeForBatteryLife() override;
    void optimizeForPerformance() override;

private:
    struct WindowsMediaAcceleratorPrivate;
    std::unique_ptr<WindowsMediaAcceleratorPrivate> d;

    void initializeDirectX();
    void initializeMediaFoundation();
    void initializeDXVA();
    void detectHardwareCapabilities();
    void enumerateGPUs();
};

} // namespace Murmur