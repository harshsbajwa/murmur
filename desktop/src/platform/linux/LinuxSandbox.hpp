#pragma once

#include "core/common/Expected.hpp"
#include "core/common/Logger.hpp"
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <memory>

namespace Murmur {

enum class LinuxSandboxError {
    InitializationFailed,
    NamespaceCreationFailed,
    CgroupCreationFailed,
    ProcessCreationFailed,
    SeccompFilterFailed,
    MountOperationFailed,
    PermissionDenied,
    ResourceLimitFailed
};

/**
 * @brief Linux-specific sandbox implementation using namespaces, cgroups, and seccomp
 * 
 * Provides process-level sandboxing on Linux using:
 * - Linux namespaces (PID, NET, MNT, UTS, IPC, USER) for isolation
 * - Control Groups (cgroups) for resource limits
 * - seccomp-bpf for system call filtering
 * - chroot/pivot_root for filesystem isolation
 * - capabilities for privilege control
 */
class LinuxSandbox : public QObject {
    Q_OBJECT

public:
    explicit LinuxSandbox(QObject* parent = nullptr);
    ~LinuxSandbox() override;

    // Core sandbox operations
    Expected<bool, LinuxSandboxError> initialize();
    Expected<bool, LinuxSandboxError> shutdown();
    bool isInitialized() const;

    // Namespace management
    Expected<bool, LinuxSandboxError> createNamespace(const QString& sandboxId, const QStringList& namespaceTypes);
    Expected<bool, LinuxSandboxError> destroyNamespace(const QString& sandboxId);
    Expected<bool, LinuxSandboxError> enterNamespace(const QString& sandboxId, qint64 processId);

    // Process creation and management
    Expected<qint64, LinuxSandboxError> createSandboxedProcess(
        const QString& executable,
        const QStringList& arguments,
        const QString& sandboxId,
        bool enableSeccomp = true,
        bool restrictCapabilities = true
    );
    Expected<bool, LinuxSandboxError> terminateProcess(qint64 processId);
    Expected<bool, LinuxSandboxError> killProcessGroup(const QString& sandboxId);

    // Resource limits via cgroups
    Expected<bool, LinuxSandboxError> createCgroup(const QString& sandboxId);
    Expected<bool, LinuxSandboxError> destroyCgroup(const QString& sandboxId);
    Expected<bool, LinuxSandboxError> setMemoryLimit(const QString& sandboxId, quint64 memoryLimitBytes);
    Expected<bool, LinuxSandboxError> setCpuLimit(const QString& sandboxId, quint32 cpuPercentage);
    Expected<bool, LinuxSandboxError> setProcessLimit(const QString& sandboxId, quint32 maxProcesses);
    Expected<bool, LinuxSandboxError> addProcessToCgroup(const QString& sandboxId, qint64 processId);

    // Security configuration
    Expected<bool, LinuxSandboxError> setupSeccompFilter(const QString& sandboxId, const QStringList& allowedSyscalls);
    Expected<bool, LinuxSandboxError> dropCapabilities(const QStringList& capabilitiesToKeep);
    Expected<bool, LinuxSandboxError> setupUserNamespace(const QString& sandboxId, quint32 uid, quint32 gid);

    // Filesystem isolation
    Expected<bool, LinuxSandboxError> setupMountNamespace(const QString& sandboxId);
    Expected<bool, LinuxSandboxError> bindMount(const QString& source, const QString& target, bool readOnly = true);
    Expected<bool, LinuxSandboxError> setupRootFilesystem(const QString& sandboxId, const QString& rootPath);
    Expected<bool, LinuxSandboxError> setFileSystemAccess(const QString& sandboxId, const QStringList& allowedPaths);

    // Network isolation
    Expected<bool, LinuxSandboxError> setupNetworkNamespace(const QString& sandboxId);
    Expected<bool, LinuxSandboxError> setNetworkAccess(const QString& sandboxId, bool enabled);
    Expected<bool, LinuxSandboxError> configureLoopbackInterface(const QString& sandboxId);

    // Monitoring and information
    Expected<QStringList, LinuxSandboxError> getActiveSandboxes() const;
    Expected<QList<qint64>, LinuxSandboxError> getSandboxProcesses(const QString& sandboxId) const;
    Expected<QPair<quint64, quint64>, LinuxSandboxError> getResourceUsage(const QString& sandboxId) const;

signals:
    void processCreated(qint64 processId, const QString& sandboxId);
    void processTerminated(qint64 processId, int exitCode);
    void resourceLimitExceeded(const QString& sandboxId, const QString& resource);
    void securityViolation(const QString& sandboxId, const QString& violation);

private:
    class LinuxSandboxPrivate;
    std::unique_ptr<LinuxSandboxPrivate> d;

    // Helper methods
    Expected<bool, LinuxSandboxError> setupNamespaces(int cloneFlags);
    Expected<bool, LinuxSandboxError> setupCgroupsV2(const QString& sandboxId);
    Expected<bool, LinuxSandboxError> installSeccompFilter(const QStringList& allowedSyscalls);
    Expected<bool, LinuxSandboxError> setupSandboxEnvironment(const QString& sandboxId);
    Expected<bool, LinuxSandboxError> waitForProcess(qint64 processId);
    void cleanupResources();
};

} // namespace Murmur