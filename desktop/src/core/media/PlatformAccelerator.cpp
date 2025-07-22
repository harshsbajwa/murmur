#include "PlatformAccelerator.hpp"
#include "../common/Logger.hpp"

#include <QtCore/QSysInfo>
#include <QtCore/QCoreApplication>

#ifdef Q_OS_MACOS
#include "../../platform/macos/MacOSMediaAccelerator.hpp"
#endif

#ifdef Q_OS_WIN
#include "../../platform/windows/WindowsMediaAccelerator.hpp"
#endif

#ifdef Q_OS_LINUX
#include "../../platform/linux/LinuxMediaAccelerator.hpp"
#endif

namespace Murmur {

PlatformAccelerator::PlatformAccelerator(QObject* parent)
    : QObject(parent)
    , currentPlatform_(getCurrentPlatform()) {
    
    Logger::instance().info("Initializing platform accelerator for: {}", currentPlatform_.toStdString());
}

QList<GPUInfo> PlatformAccelerator::getAvailableGPUs() const {
    // Default implementation returns basic GPU info
    QList<GPUInfo> gpus;
    
    GPUInfo gpu;
    gpu.name = getGPUInfo();
    gpu.isDiscrete = hasDiscreteGPU();
    gpu.vramMB = getVRAMSize();
    gpu.isActive = true; // Assume default is active
    gpu.supportsHardwareDecoding = !getSupportedDecoders().isEmpty();
    gpu.supportsHardwareEncoding = !getSupportedEncoders().isEmpty();
    gpu.supportedCodecs = getSupportedDecoders() + getSupportedEncoders();
    gpu.supportedCodecs.removeDuplicates();
    
    gpus.append(gpu);
    return gpus;
}

QString PlatformAccelerator::getCurrentPlatform() {
#ifdef Q_OS_MACOS
    return "macOS";
#elif defined(Q_OS_WIN)
    return "Windows";
#elif defined(Q_OS_LINUX)
    return "Linux";
#else
    return "Unknown";
#endif
}

bool PlatformAccelerator::isPlatformSupported(const QString& platform) {
    return getSupportedPlatforms().contains(platform);
}

QStringList PlatformAccelerator::getSupportedPlatforms() {
    QStringList platforms;
    
#ifdef Q_OS_MACOS
    platforms << "macOS";
#endif
#ifdef Q_OS_WIN
    platforms << "Windows";
#endif
#ifdef Q_OS_LINUX
    platforms << "Linux";
#endif
    
    if (platforms.isEmpty()) {
        platforms << "Generic";
    }
    
    return platforms;
}

std::unique_ptr<PlatformAccelerator> PlatformAccelerator::createForCurrentPlatform(QObject* parent) {
    QString platform = getCurrentPlatform();
    
    Logger::instance().info("Creating platform accelerator for: {}", platform.toStdString());
    
#ifdef Q_OS_MACOS
    return std::make_unique<MacOSMediaAccelerator>(parent);
#elif defined(Q_OS_WIN)
    return std::make_unique<WindowsMediaAccelerator>(parent);
#elif defined(Q_OS_LINUX)
    return std::make_unique<LinuxMediaAccelerator>(parent);
#else
    Logger::instance().warn("Unsupported platform: {}", platform.toStdString());
    return nullptr;
#endif
}

bool PlatformAccelerator::isPlatform(const QString& platform) const {
    return currentPlatform_.compare(platform, Qt::CaseInsensitive) == 0;
}

QString PlatformAccelerator::translatePlatformError(PlatformError error) const {
    switch (error) {
        case PlatformError::NotSupported:
            return "Feature not supported on this platform";
        case PlatformError::InitializationFailed:
            return "Failed to initialize platform accelerator";
        case PlatformError::ConfigurationFailed:
            return "Failed to configure hardware acceleration";
        case PlatformError::DeviceNotFound:
            return "Required hardware device not found";
        case PlatformError::InsufficientResources:
            return "Insufficient system resources";
        case PlatformError::DriverError:
            return "Graphics driver error";
        case PlatformError::UnknownError:
        default:
            return "Unknown platform error";
    }
}

void PlatformAccelerator::emitError(PlatformError error, const QString& context) {
    QString errorMessage = translatePlatformError(error);
    if (!context.isEmpty()) {
        errorMessage = QString("%1: %2").arg(context).arg(errorMessage);
    }
    
    Logger::instance().error("{}", errorMessage.toStdString());
    emit errorOccurred(error, errorMessage);
}

} // namespace Murmur