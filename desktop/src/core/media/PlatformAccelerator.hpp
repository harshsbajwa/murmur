#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include "../common/Expected.hpp"

namespace Murmur {

enum class PlatformError {
    NotSupported,
    InitializationFailed,
    ConfigurationFailed,
    DeviceNotFound,
    InsufficientResources,
    DriverError,
    UnknownError
};

struct EncoderSettings {
    int width = 1920;
    int height = 1080;
    int frameRate = 30;
    int bitrate = 5000; // kbps
    QString profile = "high";
    QString preset = "medium";
    bool useHardwareAcceleration = true;
    bool enableBFrames = true;
    int keyFrameInterval = 60;
    QString pixelFormat = "yuv420p";
};

struct DecoderSettings {
    bool useHardwareAcceleration = true;
    bool enableMultithreading = true;
    int maxThreads = 0; // 0 = auto
    QString outputPixelFormat = "yuv420p";
};

struct GPUInfo {
    QString name;
    QString vendor;
    QString driverVersion;
    int vramMB = 0;
    bool isDiscrete = false;
    bool isActive = false;
    bool supportsHardwareDecoding = false;
    bool supportsHardwareEncoding = false;
    QStringList supportedCodecs;
};

/**
 * @brief Abstract base class for platform-specific media acceleration
 * 
 * Provides a unified interface for hardware acceleration across different platforms,
 * with platform-specific implementations for optimal performance.
 */
class PlatformAccelerator : public QObject {
    Q_OBJECT

public:
    explicit PlatformAccelerator(QObject* parent = nullptr);
    virtual ~PlatformAccelerator() = default;

    // Hardware capability detection
    virtual bool isHardwareDecodingSupported(const QString& codec) const = 0;
    virtual bool isHardwareEncodingSupported(const QString& codec) const = 0;
    virtual QStringList getSupportedDecoders() const = 0;
    virtual QStringList getSupportedEncoders() const = 0;

    // GPU information
    virtual QString getGPUInfo() const = 0;
    virtual bool hasDiscreteGPU() const = 0;
    virtual int getVRAMSize() const = 0;
    virtual QList<GPUInfo> getAvailableGPUs() const;

    // Hardware acceleration setup
    virtual bool initializeDecoder(const QString& codec) = 0;
    virtual bool initializeEncoder(const QString& codec, const EncoderSettings& settings) = 0;
    virtual void cleanup() = 0;

    // Performance optimization
    virtual void optimizeForBatteryLife() = 0;
    virtual void optimizeForPerformance() = 0;
    virtual void setPreferredGPU(const QString& gpuName) = 0;

    // Platform detection
    static QString getCurrentPlatform();
    static bool isPlatformSupported(const QString& platform);
    static QStringList getSupportedPlatforms();

    // Factory method
    static std::unique_ptr<PlatformAccelerator> createForCurrentPlatform(QObject* parent = nullptr);

signals:
    void hardwareAccelerationChanged(bool enabled);
    void gpuChanged(const QString& gpuName);
    void errorOccurred(PlatformError error, const QString& message);

protected:
    // Helper methods for implementations
    bool isPlatform(const QString& platform) const;
    QString translatePlatformError(PlatformError error) const;
    void emitError(PlatformError error, const QString& context = QString());

private:
    QString currentPlatform_;
};

} // namespace Murmur