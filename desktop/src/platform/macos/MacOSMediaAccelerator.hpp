#pragma once

#include "../../core/media/PlatformAccelerator.hpp"
#include <QtCore/QObject>
#include <QtCore/QString>

namespace Murmur {

class MacOSMediaAccelerator : public PlatformAccelerator {
    Q_OBJECT

public:
    explicit MacOSMediaAccelerator(QObject* parent = nullptr);
    ~MacOSMediaAccelerator() override;

    // Hardware acceleration capabilities
    bool isHardwareDecodingSupported(const QString& codec) const override;
    bool isHardwareEncodingSupported(const QString& codec) const override;
    QStringList getSupportedDecoders() const override;
    QStringList getSupportedEncoders() const override;

    // GPU information
    QString getGPUInfo() const override;
    bool hasDiscreteGPU() const override;
    int getVRAMSize() const override;

    // Hardware acceleration setup
    bool initializeDecoder(const QString& codec) override;
    bool initializeEncoder(const QString& codec, const EncoderSettings& settings) override;
    void cleanup() override;

    // macOS-specific features
    bool isMetalSupported() const;
    bool isVideoToolboxAvailable() const;
    QString getMetalDeviceInfo() const;
    
    // Performance optimization
    void optimizeForBatteryLife() override;
    void optimizeForPerformance() override;
    void setPreferredGPU(const QString& gpuName) override;

private:
    struct MacOSMediaAcceleratorPrivate;
    std::unique_ptr<MacOSMediaAcceleratorPrivate> d;

    bool checkVideoToolboxSupport() const;
    bool checkMetalSupport() const;
    QList<GPUInfo> getAvailableGPUs() const override;
    void configureVideoToolbox();
    void configureMetalDevice();
};

} // namespace Murmur