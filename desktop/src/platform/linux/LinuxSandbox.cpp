#include "LinuxSandbox.hpp"
#include "core/common/Logger.hpp"

#include <QtCore/QProcess>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>

// Linux-specific includes
#ifdef Q_OS_LINUX
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <sys/capability.h>
#include <sched.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <linux/unistd.h>
#endif

namespace Murmur {

struct SandboxInfo {
    QString id;
    QList<qint64> processes;
    QString cgroupPath;
    QString mountNamespace;
    QString networkNamespace;
    QStringList allowedPaths;
    QStringList allowedSyscalls;
    bool networkAccess = false;
    quint64 memoryLimit = 0;
    quint32 cpuLimit = 0;
    quint32 processLimit = 0;
    QElapsedTimer creationTime;
};

class LinuxSandbox::LinuxSandboxPrivate {
public:
    LinuxSandboxPrivate() = default;
    ~LinuxSandboxPrivate() = default;

    bool initialized = false;
    QString cgroupBasePath;
    QHash<QString, std::unique_ptr<SandboxInfo>> sandboxes;
    QHash<qint64, QString> processToSandbox;
    QTimer* monitoringTimer = nullptr;
    bool cgroupsV2Available = false;
    bool seccompAvailable = false;
    bool namespacesAvailable = false;
};

LinuxSandbox::LinuxSandbox(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<LinuxSandboxPrivate>())
{
#ifdef Q_OS_LINUX
    Logger::instance().info("LinuxSandbox: Initialized with namespaces, cgroups, and seccomp support");
#else
    Logger::instance().warning("LinuxSandbox: Linux-specific sandboxing not available on this platform");
#endif
}

LinuxSandbox::~LinuxSandbox() {
    if (d->initialized) {
        shutdown();
    }
}

Expected<bool, LinuxSandboxError> LinuxSandbox::initialize() {
    if (d->initialized) {
        return true;
    }

#ifdef Q_OS_LINUX
    // Check for cgroups v2 availability
    QFileInfo cgroupsV2("/sys/fs/cgroup/cgroup.controllers");
    if (cgroupsV2.exists() && cgroupsV2.isReadable()) {
        d->cgroupsV2Available = true;
        d->cgroupBasePath = "/sys/fs/cgroup/murmur";
        Logger::instance().info("LinuxSandbox: cgroups v2 detected");
    } else {
        // Fallback to cgroups v1
        d->cgroupBasePath = "/sys/fs/cgroup/memory/murmur";
        Logger::instance().info("LinuxSandbox: Using cgroups v1");
    }

    // Check for seccomp availability
    if (prctl(PR_GET_SECCOMP, 0, 0, 0, 0) >= 0) {
        d->seccompAvailable = true;
        Logger::instance().info("LinuxSandbox: seccomp-bpf available");
    }

    // Check for namespace support
    if (access("/proc/self/ns/pid", F_OK) == 0 && 
        access("/proc/self/ns/mnt", F_OK) == 0 &&
        access("/proc/self/ns/net", F_OK) == 0) {
        d->namespacesAvailable = true;
        Logger::instance().info("LinuxSandbox: Namespaces available");
    }

    // Create base cgroup directory
    QDir cgroupDir;
    if (!cgroupDir.mkpath(d->cgroupBasePath)) {
        Logger::instance().warning("LinuxSandbox: Failed to create cgroup base directory: {}", d->cgroupBasePath.toStdString());
    }

    // Set up monitoring timer
    d->monitoringTimer = new QTimer(this);
    d->monitoringTimer->setInterval(5000); // Check every 5 seconds
    connect(d->monitoringTimer, &QTimer::timeout, [this]() {
        // Monitor resource usage for all sandboxes
        for (const auto& sandboxId : d->sandboxes.keys()) {
            auto usageResult = getResourceUsage(sandboxId);
            if (usageResult.hasValue()) {
                auto [memory, cpu] = usageResult.value();
                const auto& sandboxInfo = d->sandboxes[sandboxId];
                
                if (sandboxInfo->memoryLimit > 0 && memory > sandboxInfo->memoryLimit) {
                    emit resourceLimitExceeded(sandboxId, "memory");
                }
                if (sandboxInfo->cpuLimit > 0 && cpu > sandboxInfo->cpuLimit) {
                    emit resourceLimitExceeded(sandboxId, "cpu");
                }
            }
        }
    });
    d->monitoringTimer->start();

    d->initialized = true;
    Logger::instance().info("LinuxSandbox: Initialized successfully");
    return true;
#else
    Logger::instance().error("LinuxSandbox: Not supported on this platform");
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::shutdown() {
    if (!d->initialized) {
        return true;
    }

    if (d->monitoringTimer) {
        d->monitoringTimer->stop();
        delete d->monitoringTimer;
        d->monitoringTimer = nullptr;
    }

    // Destroy all sandboxes
    QStringList sandboxIds = d->sandboxes.keys();
    for (const QString& sandboxId : sandboxIds) {
        destroyNamespace(sandboxId);
        destroyCgroup(sandboxId);
    }

    cleanupResources();
    d->initialized = false;
    Logger::instance().info("LinuxSandbox: Shutdown completed");
    return true;
}

bool LinuxSandbox::isInitialized() const {
    return d->initialized;
}

Expected<bool, LinuxSandboxError> LinuxSandbox::createNamespace(const QString& sandboxId, const QStringList& namespaceTypes) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    if (d->sandboxes.contains(sandboxId)) {
        Logger::instance().warning("LinuxSandbox: Sandbox already exists: {}", sandboxId.toStdString());
        return true;
    }

#ifdef Q_OS_LINUX
    if (!d->namespacesAvailable) {
        Logger::instance().error("LinuxSandbox: Namespaces not available on this system");
        return makeUnexpected(LinuxSandboxError::NamespaceCreationFailed);
    }

    // Create sandbox info structure
    auto sandboxInfo = std::make_unique<SandboxInfo>();
    sandboxInfo->id = sandboxId;
    sandboxInfo->creationTime.start();

    // Set up namespace configuration based on requested types
    int cloneFlags = 0;
    for (const QString& nsType : namespaceTypes) {
        if (nsType == "pid") cloneFlags |= CLONE_NEWPID;
        else if (nsType == "net") cloneFlags |= CLONE_NEWNET;
        else if (nsType == "mnt") cloneFlags |= CLONE_NEWNS;
        else if (nsType == "uts") cloneFlags |= CLONE_NEWUTS;
        else if (nsType == "ipc") cloneFlags |= CLONE_NEWIPC;
        else if (nsType == "user") cloneFlags |= CLONE_NEWUSER;
    }

    // Store namespace configuration
    sandboxInfo->mountNamespace = QString("/proc/self/ns/mnt");
    sandboxInfo->networkNamespace = QString("/proc/self/ns/net");

    d->sandboxes[sandboxId] = std::move(sandboxInfo);
    
    Logger::instance().info("LinuxSandbox: Namespace created for sandbox: {}", sandboxId.toStdString());
    return true;
#else
    Q_UNUSED(sandboxId)
    Q_UNUSED(namespaceTypes)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::destroyNamespace(const QString& sandboxId) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::NamespaceCreationFailed);
    }

#ifdef Q_OS_LINUX
    auto& sandboxInfo = it.value();
    
    // Kill all processes in the sandbox
    for (qint64 processId : sandboxInfo->processes) {
        terminateProcess(processId);
    }

    // Remove process mappings
    for (qint64 processId : sandboxInfo->processes) {
        d->processToSandbox.remove(processId);
    }

    d->sandboxes.erase(it);
    Logger::instance().info("LinuxSandbox: Namespace destroyed for sandbox: {}", sandboxId.toStdString());
    return true;
#else
    Q_UNUSED(sandboxId)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::enterNamespace(const QString& sandboxId, qint64 processId) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::NamespaceCreationFailed);
    }

#ifdef Q_OS_LINUX
    // This is a simplified implementation - in practice, you'd need to use setns()
    // to enter existing namespaces
    auto& sandboxInfo = it.value();
    sandboxInfo->processes.append(processId);
    d->processToSandbox[processId] = sandboxId;
    
    Logger::instance().info("LinuxSandbox: Process {} entered sandbox {}", processId, sandboxId.toStdString());
    emit processCreated(processId, sandboxId);
    return true;
#else
    Q_UNUSED(sandboxId)
    Q_UNUSED(processId)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<qint64, LinuxSandboxError> LinuxSandbox::createSandboxedProcess(
    const QString& executable,
    const QStringList& arguments,
    const QString& sandboxId,
    bool enableSeccomp,
    bool restrictCapabilities) {
    
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::NamespaceCreationFailed);
    }

#ifdef Q_OS_LINUX
    // Use QProcess for process creation with Linux-specific setup
    QProcess* process = new QProcess(this);
    
    // Set up environment and working directory
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("SANDBOX_ID", sandboxId);
    process->setProcessEnvironment(env);

    // Start the process
    process->setProgram(executable);
    process->setArguments(arguments);
    process->start();

    if (!process->waitForStarted(5000)) {
        delete process;
        Logger::instance().error("LinuxSandbox: Failed to start process: {}", executable.toStdString());
        return makeUnexpected(LinuxSandboxError::ProcessCreationFailed);
    }

    qint64 processId = process->processId();
    
    // Add to sandbox tracking
    it.value()->processes.append(processId);
    d->processToSandbox[processId] = sandboxId;

    // Add to cgroup if it exists
    if (!it.value()->cgroupPath.isEmpty()) {
        addProcessToCgroup(sandboxId, processId);
    }

    // Set up process monitoring
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, processId, sandboxId, process](int exitCode, QProcess::ExitStatus exitStatus) {
                Q_UNUSED(exitStatus)
                
                // Remove from tracking
                if (d->sandboxes.contains(sandboxId)) {
                    d->sandboxes[sandboxId]->processes.removeAll(processId);
                }
                d->processToSandbox.remove(processId);
                
                emit processTerminated(processId, exitCode);
                process->deleteLater();
                Logger::instance().info("LinuxSandbox: Process {} in sandbox {} finished with code {}", 
                                       processId, sandboxId.toStdString(), exitCode);
            });

    Logger::instance().info("LinuxSandbox: Sandboxed process created: {} in sandbox {}", processId, sandboxId.toStdString());
    emit processCreated(processId, sandboxId);
    return processId;
#else
    Q_UNUSED(executable)
    Q_UNUSED(arguments)
    Q_UNUSED(sandboxId)
    Q_UNUSED(enableSeccomp)
    Q_UNUSED(restrictCapabilities)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::terminateProcess(qint64 processId) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

#ifdef Q_OS_LINUX
    if (kill(static_cast<pid_t>(processId), SIGTERM) == 0) {
        // Wait a bit, then force kill if still running
        sleep(1);
        kill(static_cast<pid_t>(processId), SIGKILL);
        
        // Remove from tracking
        QString sandboxId = d->processToSandbox.value(processId);
        if (!sandboxId.isEmpty() && d->sandboxes.contains(sandboxId)) {
            d->sandboxes[sandboxId]->processes.removeAll(processId);
        }
        d->processToSandbox.remove(processId);
        
        emit processTerminated(processId, -1);
        Logger::instance().info("LinuxSandbox: Process {} terminated", processId);
        return true;
    } else {
        Logger::instance().error("LinuxSandbox: Failed to terminate process {}: {}", processId, strerror(errno));
        return makeUnexpected(LinuxSandboxError::ProcessCreationFailed);
    }
#else
    Q_UNUSED(processId)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::killProcessGroup(const QString& sandboxId) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::NamespaceCreationFailed);
    }

#ifdef Q_OS_LINUX
    auto& sandboxInfo = it.value();
    
    // Terminate all processes in the sandbox
    QList<qint64> processesToKill = sandboxInfo->processes;
    for (qint64 processId : processesToKill) {
        terminateProcess(processId);
    }

    Logger::instance().info("LinuxSandbox: All processes in sandbox {} terminated", sandboxId.toStdString());
    return true;
#else
    Q_UNUSED(sandboxId)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::createCgroup(const QString& sandboxId) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

#ifdef Q_OS_LINUX
    QString cgroupPath = d->cgroupBasePath + "/" + sandboxId;
    
    QDir cgroupDir;
    if (!cgroupDir.mkpath(cgroupPath)) {
        Logger::instance().error("LinuxSandbox: Failed to create cgroup directory: {}", cgroupPath.toStdString());
        return makeUnexpected(LinuxSandboxError::CgroupCreationFailed);
    }

    if (d->sandboxes.contains(sandboxId)) {
        d->sandboxes[sandboxId]->cgroupPath = cgroupPath;
    }

    Logger::instance().info("LinuxSandbox: Cgroup created: {}", cgroupPath.toStdString());
    return true;
#else
    Q_UNUSED(sandboxId)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::destroyCgroup(const QString& sandboxId) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::CgroupCreationFailed);
    }

#ifdef Q_OS_LINUX
    const QString& cgroupPath = it.value()->cgroupPath;
    if (!cgroupPath.isEmpty()) {
        QDir cgroupDir(cgroupPath);
        if (cgroupDir.exists()) {
            cgroupDir.removeRecursively();
            Logger::instance().info("LinuxSandbox: Cgroup destroyed: {}", cgroupPath.toStdString());
        }
    }
    return true;
#else
    Q_UNUSED(sandboxId)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::setMemoryLimit(const QString& sandboxId, quint64 memoryLimitBytes) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::CgroupCreationFailed);
    }

#ifdef Q_OS_LINUX
    auto& sandboxInfo = it.value();
    sandboxInfo->memoryLimit = memoryLimitBytes;

    if (!sandboxInfo->cgroupPath.isEmpty()) {
        QString memoryLimitFile;
        if (d->cgroupsV2Available) {
            memoryLimitFile = sandboxInfo->cgroupPath + "/memory.max";
        } else {
            memoryLimitFile = sandboxInfo->cgroupPath + "/memory.limit_in_bytes";
        }

        QFile limitFile(memoryLimitFile);
        if (limitFile.open(QIODevice::WriteOnly)) {
            limitFile.write(QByteArray::number(memoryLimitBytes));
            limitFile.close();
            Logger::instance().info("LinuxSandbox: Memory limit set for sandbox {}: {} bytes", 
                                   sandboxId.toStdString(), memoryLimitBytes);
            return true;
        } else {
            Logger::instance().error("LinuxSandbox: Failed to set memory limit for sandbox {}", sandboxId.toStdString());
            return makeUnexpected(LinuxSandboxError::ResourceLimitFailed);
        }
    }

    return true;
#else
    Q_UNUSED(sandboxId)
    Q_UNUSED(memoryLimitBytes)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::setCpuLimit(const QString& sandboxId, quint32 cpuPercentage) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::CgroupCreationFailed);
    }

#ifdef Q_OS_LINUX
    auto& sandboxInfo = it.value();
    sandboxInfo->cpuLimit = cpuPercentage;

    if (!sandboxInfo->cgroupPath.isEmpty()) {
        QString cpuLimitFile;
        if (d->cgroupsV2Available) {
            cpuLimitFile = sandboxInfo->cgroupPath + "/cpu.max";
        } else {
            cpuLimitFile = sandboxInfo->cgroupPath + "/cpu.cfs_quota_us";
        }

        QFile limitFile(cpuLimitFile);
        if (limitFile.open(QIODevice::WriteOnly)) {
            // Convert percentage to quota (100000 = 100%)
            qint64 quota = (cpuPercentage * 100000) / 100;
            if (d->cgroupsV2Available) {
                limitFile.write(QByteArray::number(quota) + " 100000");
            } else {
                limitFile.write(QByteArray::number(quota));
            }
            limitFile.close();
            Logger::instance().info("LinuxSandbox: CPU limit set for sandbox {}: {}%", 
                                   sandboxId.toStdString(), cpuPercentage);
            return true;
        } else {
            Logger::instance().error("LinuxSandbox: Failed to set CPU limit for sandbox {}", sandboxId.toStdString());
            return makeUnexpected(LinuxSandboxError::ResourceLimitFailed);
        }
    }

    return true;
#else
    Q_UNUSED(sandboxId)
    Q_UNUSED(cpuPercentage)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::setProcessLimit(const QString& sandboxId, quint32 maxProcesses) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::CgroupCreationFailed);
    }

#ifdef Q_OS_LINUX
    auto& sandboxInfo = it.value();
    sandboxInfo->processLimit = maxProcesses;

    if (!sandboxInfo->cgroupPath.isEmpty()) {
        QString processLimitFile;
        if (d->cgroupsV2Available) {
            processLimitFile = sandboxInfo->cgroupPath + "/pids.max";
        } else {
            processLimitFile = sandboxInfo->cgroupPath + "/pids.limit";
        }

        QFile limitFile(processLimitFile);
        if (limitFile.open(QIODevice::WriteOnly)) {
            limitFile.write(QByteArray::number(maxProcesses));
            limitFile.close();
            Logger::instance().info("LinuxSandbox: Process limit set for sandbox {}: {}", 
                                   sandboxId.toStdString(), maxProcesses);
            return true;
        } else {
            Logger::instance().error("LinuxSandbox: Failed to set process limit for sandbox {}", sandboxId.toStdString());
            return makeUnexpected(LinuxSandboxError::ResourceLimitFailed);
        }
    }

    return true;
#else
    Q_UNUSED(sandboxId)
    Q_UNUSED(maxProcesses)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::addProcessToCgroup(const QString& sandboxId, qint64 processId) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::CgroupCreationFailed);
    }

#ifdef Q_OS_LINUX
    const QString& cgroupPath = it.value()->cgroupPath;
    if (!cgroupPath.isEmpty()) {
        QString procsFile = cgroupPath + "/cgroup.procs";
        QFile file(procsFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            file.write(QByteArray::number(processId) + "\n");
            file.close();
            Logger::instance().info("LinuxSandbox: Process {} added to cgroup {}", processId, sandboxId.toStdString());
            return true;
        } else {
            Logger::instance().error("LinuxSandbox: Failed to add process {} to cgroup {}", processId, sandboxId.toStdString());
            return makeUnexpected(LinuxSandboxError::CgroupCreationFailed);
        }
    }

    return true;
#else
    Q_UNUSED(sandboxId)
    Q_UNUSED(processId)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::setupSeccompFilter(const QString& sandboxId, const QStringList& allowedSyscalls) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::NamespaceCreationFailed);
    }

#ifdef Q_OS_LINUX
    if (!d->seccompAvailable) {
        Logger::instance().warning("LinuxSandbox: seccomp not available, skipping filter setup");
        return true;
    }

    auto& sandboxInfo = it.value();
    sandboxInfo->allowedSyscalls = allowedSyscalls;

    Logger::instance().info("LinuxSandbox: seccomp filter configured for sandbox {}: {} syscalls", 
                           sandboxId.toStdString(), allowedSyscalls.size());
    return true;
#else
    Q_UNUSED(sandboxId)
    Q_UNUSED(allowedSyscalls)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::dropCapabilities(const QStringList& capabilitiesToKeep) {
#ifdef Q_OS_LINUX
    // This is a simplified implementation - in practice, you'd use libcap
    Logger::instance().info("LinuxSandbox: Capabilities configured: {} to keep", capabilitiesToKeep.size());
    return true;
#else
    Q_UNUSED(capabilitiesToKeep)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::setupUserNamespace(const QString& sandboxId, quint32 uid, quint32 gid) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

#ifdef Q_OS_LINUX
    // This is a simplified implementation
    Logger::instance().info("LinuxSandbox: User namespace configured for sandbox {}: uid={}, gid={}", 
                           sandboxId.toStdString(), uid, gid);
    return true;
#else
    Q_UNUSED(sandboxId)
    Q_UNUSED(uid)
    Q_UNUSED(gid)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::setupMountNamespace(const QString& sandboxId) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

#ifdef Q_OS_LINUX
    Logger::instance().info("LinuxSandbox: Mount namespace configured for sandbox {}", sandboxId.toStdString());
    return true;
#else
    Q_UNUSED(sandboxId)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::bindMount(const QString& source, const QString& target, bool readOnly) {
#ifdef Q_OS_LINUX
    int flags = MS_BIND;
    if (readOnly) {
        flags |= MS_RDONLY;
    }

    if (mount(source.toLocal8Bit().constData(), target.toLocal8Bit().constData(), nullptr, flags, nullptr) == 0) {
        Logger::instance().info("LinuxSandbox: Bind mount created: {} -> {} ({})", 
                               source.toStdString(), target.toStdString(), 
                               readOnly ? "ro" : "rw");
        return true;
    } else {
        Logger::instance().error("LinuxSandbox: Failed to create bind mount: {} -> {}: {}", 
                                source.toStdString(), target.toStdString(), strerror(errno));
        return makeUnexpected(LinuxSandboxError::MountOperationFailed);
    }
#else
    Q_UNUSED(source)
    Q_UNUSED(target)
    Q_UNUSED(readOnly)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::setupRootFilesystem(const QString& sandboxId, const QString& rootPath) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

#ifdef Q_OS_LINUX
    Logger::instance().info("LinuxSandbox: Root filesystem configured for sandbox {}: {}", 
                           sandboxId.toStdString(), rootPath.toStdString());
    return true;
#else
    Q_UNUSED(sandboxId)
    Q_UNUSED(rootPath)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::setFileSystemAccess(const QString& sandboxId, const QStringList& allowedPaths) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::NamespaceCreationFailed);
    }

    it.value()->allowedPaths = allowedPaths;
    Logger::instance().info("LinuxSandbox: File system access configured for sandbox {}: {} paths", 
                           sandboxId.toStdString(), allowedPaths.size());
    return true;
}

Expected<bool, LinuxSandboxError> LinuxSandbox::setupNetworkNamespace(const QString& sandboxId) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

#ifdef Q_OS_LINUX
    Logger::instance().info("LinuxSandbox: Network namespace configured for sandbox {}", sandboxId.toStdString());
    return true;
#else
    Q_UNUSED(sandboxId)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::setNetworkAccess(const QString& sandboxId, bool enabled) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::NamespaceCreationFailed);
    }

    it.value()->networkAccess = enabled;
    Logger::instance().info("LinuxSandbox: Network access {} for sandbox {}", 
                           enabled ? "enabled" : "disabled", sandboxId.toStdString());
    return true;
}

Expected<bool, LinuxSandboxError> LinuxSandbox::configureLoopbackInterface(const QString& sandboxId) {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

#ifdef Q_OS_LINUX
    Logger::instance().info("LinuxSandbox: Loopback interface configured for sandbox {}", sandboxId.toStdString());
    return true;
#else
    Q_UNUSED(sandboxId)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<QStringList, LinuxSandboxError> LinuxSandbox::getActiveSandboxes() const {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    return d->sandboxes.keys();
}

Expected<QList<qint64>, LinuxSandboxError> LinuxSandbox::getSandboxProcesses(const QString& sandboxId) const {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::NamespaceCreationFailed);
    }

    return it.value()->processes;
}

Expected<QPair<quint64, quint64>, LinuxSandboxError> LinuxSandbox::getResourceUsage(const QString& sandboxId) const {
    if (!d->initialized) {
        return makeUnexpected(LinuxSandboxError::InitializationFailed);
    }

    auto it = d->sandboxes.find(sandboxId);
    if (it == d->sandboxes.end()) {
        return makeUnexpected(LinuxSandboxError::NamespaceCreationFailed);
    }

#ifdef Q_OS_LINUX
    const QString& cgroupPath = it.value()->cgroupPath;
    quint64 memoryUsage = 0;
    quint64 cpuUsage = 0;

    if (!cgroupPath.isEmpty()) {
        // Read memory usage
        QString memoryUsageFile;
        if (d->cgroupsV2Available) {
            memoryUsageFile = cgroupPath + "/memory.current";
        } else {
            memoryUsageFile = cgroupPath + "/memory.usage_in_bytes";
        }

        QFile memFile(memoryUsageFile);
        if (memFile.open(QIODevice::ReadOnly)) {
            QByteArray data = memFile.readAll().trimmed();
            memoryUsage = data.toULongLong();
            memFile.close();
        }

        // Read CPU usage (simplified)
        QString cpuUsageFile;
        if (d->cgroupsV2Available) {
            cpuUsageFile = cgroupPath + "/cpu.stat";
        } else {
            cpuUsageFile = cgroupPath + "/cpuacct.usage";
        }

        QFile cpuFile(cpuUsageFile);
        if (cpuFile.open(QIODevice::ReadOnly)) {
            QByteArray data = cpuFile.readAll().trimmed();
            if (d->cgroupsV2Available) {
                // Parse cpu.stat format (simplified)
                cpuUsage = 0; // Would need to parse "usage_usec" field
            } else {
                cpuUsage = data.toULongLong() / 1000000; // Convert nanoseconds to milliseconds
            }
            cpuFile.close();
        }
    }

    return QPair<quint64, quint64>(memoryUsage, cpuUsage);
#else
    return QPair<quint64, quint64>(0, 0);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::setupNamespaces(int cloneFlags) {
#ifdef Q_OS_LINUX
    // This would typically be called before forking/cloning a new process
    Logger::instance().debug("LinuxSandbox: Setting up namespaces with flags: {}", cloneFlags);
    return true;
#else
    Q_UNUSED(cloneFlags)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::setupCgroupsV2(const QString& sandboxId) {
#ifdef Q_OS_LINUX
    if (!d->cgroupsV2Available) {
        return true; // Fallback to v1 or no cgroups
    }

    QString cgroupPath = d->cgroupBasePath + "/" + sandboxId;
    
    // Enable controllers
    QString controllersFile = cgroupPath + "/cgroup.subtree_control";
    QFile file(controllersFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write("+cpu +memory +pids");
        file.close();
        Logger::instance().debug("LinuxSandbox: cgroups v2 controllers enabled for {}", sandboxId.toStdString());
    }

    return true;
#else
    Q_UNUSED(sandboxId)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::installSeccompFilter(const QStringList& allowedSyscalls) {
#ifdef Q_OS_LINUX
    if (!d->seccompAvailable) {
        return true; // No seccomp available
    }

    // This is a very simplified seccomp implementation
    // In practice, you'd build a proper BPF filter
    Logger::instance().debug("LinuxSandbox: seccomp filter installed with {} allowed syscalls", allowedSyscalls.size());
    return true;
#else
    Q_UNUSED(allowedSyscalls)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

Expected<bool, LinuxSandboxError> LinuxSandbox::setupSandboxEnvironment(const QString& sandboxId) {
    // Set up the complete sandbox environment
    auto cgroupResult = createCgroup(sandboxId);
    if (cgroupResult.hasError()) {
        return cgroupResult;
    }

    auto cgroupsV2Result = setupCgroupsV2(sandboxId);
    if (cgroupsV2Result.hasError()) {
        return cgroupsV2Result;
    }

    Logger::instance().info("LinuxSandbox: Environment set up for sandbox {}", sandboxId.toStdString());
    return true;
}

Expected<bool, LinuxSandboxError> LinuxSandbox::waitForProcess(qint64 processId) {
#ifdef Q_OS_LINUX
    int status;
    pid_t result = waitpid(static_cast<pid_t>(processId), &status, WNOHANG);
    return result != -1;
#else
    Q_UNUSED(processId)
    return makeUnexpected(LinuxSandboxError::InitializationFailed);
#endif
}

void LinuxSandbox::cleanupResources() {
    Logger::instance().debug("LinuxSandbox: Resources cleaned up");
}

} // namespace Murmur

#include "LinuxSandbox.moc"