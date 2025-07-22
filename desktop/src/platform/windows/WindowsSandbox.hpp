#pragma once

#include "core/common/Expected.hpp"
#include "core/common/Logger.hpp"
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <memory>

namespace Murmur {

enum class WindowsSandboxError {
    InitializationFailed,
    JobObjectCreationFailed,
    ProcessCreationFailed,
    AccessTokenCreationFailed,
    IntegrityLevelFailed,
    AppContainerFailed,
    PermissionDenied
};

/**
 * @brief Windows-specific sandbox implementation using Job Objects and security features
 * 
 * Provides process-level sandboxing on Windows using:
 * - Job Objects for resource limits and process isolation
 * - Restricted tokens for privilege reduction
 * - Low integrity levels for untrusted processes
 * - Security descriptors for access control
 */
class WindowsSandbox : public QObject {
    Q_OBJECT

public:
    explicit WindowsSandbox(QObject* parent = nullptr);
    ~WindowsSandbox() override;

    // Core sandbox operations
    Expected<bool, WindowsSandboxError> initialize();
    Expected<bool, WindowsSandboxError> shutdown();
    bool isInitialized() const;

    // Job object management
    Expected<bool, WindowsSandboxError> createJobObject(const QString& jobName);
    Expected<bool, WindowsSandboxError> destroyJobObject(const QString& jobName);
    Expected<bool, WindowsSandboxError> addProcessToJob(const QString& jobName, qint64 processId);

    // Process creation and management
    Expected<qint64, WindowsSandboxError> createSandboxedProcess(
        const QString& executable,
        const QStringList& arguments,
        const QString& jobName,
        bool lowIntegrity = true,
        bool restrictedToken = true
    );
    Expected<bool, WindowsSandboxError> terminateProcess(qint64 processId);

    // Resource limits
    Expected<bool, WindowsSandboxError> setMemoryLimit(const QString& jobName, quint64 memoryLimitBytes);
    Expected<bool, WindowsSandboxError> setCpuLimit(const QString& jobName, quint32 cpuPercentage);
    Expected<bool, WindowsSandboxError> setProcessLimit(const QString& jobName, quint32 maxProcesses);

    // Security configuration
    Expected<bool, WindowsSandboxError> enableLowIntegrityLevel(bool enabled);
    Expected<bool, WindowsSandboxError> configureRestrictedToken(const QStringList& deniedSids);

    // Access control
    Expected<bool, WindowsSandboxError> setFileSystemAccess(const QString& jobName, const QStringList& allowedPaths);
    Expected<bool, WindowsSandboxError> setNetworkAccess(const QString& jobName, bool enabled);

    // Monitoring and information
    Expected<QStringList, WindowsSandboxError> getActiveJobs() const;
    Expected<QList<qint64>, WindowsSandboxError> getJobProcesses(const QString& jobName) const;
    Expected<QPair<quint64, quint64>, WindowsSandboxError> getResourceUsage(const QString& jobName) const;

signals:
    void processCreated(qint64 processId, const QString& jobName);
    void processTerminated(qint64 processId, int exitCode);
    void resourceLimitExceeded(const QString& jobName, const QString& resource);
    void securityViolation(const QString& jobName, const QString& violation);

private:
    class WindowsSandboxPrivate;
    std::unique_ptr<WindowsSandboxPrivate> d;

    // Helper methods
    Expected<bool, WindowsSandboxError> setupJobObjectSecurity(void* jobHandle);
    Expected<void*, WindowsSandboxError> createRestrictedToken();
    Expected<bool, WindowsSandboxError> setProcessIntegrityLevel(void* processHandle, bool lowIntegrity);
    Expected<bool, WindowsSandboxError> configureJobObjectLimits(void* jobHandle, const QString& jobName);
    void cleanupResources();
};

} // namespace Murmur