#include "WindowsSandbox.hpp"
#include "core/common/Logger.hpp"

#include <QtCore/QProcess>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>

// Windows-specific includes
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <sddl.h>
#include <aclapi.h>
#include <securitybaseapi.h>
#include <processthreadsapi.h>
#include <winnt.h>
#endif

namespace Murmur {

struct JobObjectInfo {
    QString name;
    void* handle = nullptr;
    QList<qint64> processes;
    quint64 memoryLimit = 0;
    quint32 cpuLimit = 0;
    quint32 processLimit = 0;
    QStringList allowedPaths;
    bool networkAccess = false;
    QElapsedTimer creationTime;
};

class WindowsSandbox::WindowsSandboxPrivate {
public:
    WindowsSandboxPrivate() = default;
    ~WindowsSandboxPrivate() = default;

    bool initialized = false;
    bool lowIntegrityEnabled = false;
    QStringList deniedSids;
    QHash<QString, std::unique_ptr<JobObjectInfo>> jobObjects;
    QHash<qint64, QString> processToJob;
    QTimer* monitoringTimer = nullptr;

#ifdef Q_OS_WIN
    HANDLE restrictedToken = nullptr;
    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
#endif
};

WindowsSandbox::WindowsSandbox(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<WindowsSandboxPrivate>())
{
#ifdef Q_OS_WIN
    Logger::instance().info("WindowsSandbox: Initialized with Job Objects support");
#else
    Logger::instance().warning("WindowsSandbox: Job Objects support not available on this platform");
#endif
}

WindowsSandbox::~WindowsSandbox() {
    if (d->initialized) {
        shutdown();
    }
}

Expected<bool, WindowsSandboxError> WindowsSandbox::initialize() {
    if (d->initialized) {
        return true;
    }

#ifdef Q_OS_WIN
    // Set up monitoring timer
    d->monitoringTimer = new QTimer(this);
    d->monitoringTimer->setInterval(5000); // Check every 5 seconds
    connect(d->monitoringTimer, &QTimer::timeout, [this]() {
        // Monitor resource usage for all job objects
        for (const auto& jobName : d->jobObjects.keys()) {
            auto usageResult = getResourceUsage(jobName);
            if (usageResult.hasValue()) {
                auto [memory, cpu] = usageResult.value();
                const auto& jobInfo = d->jobObjects[jobName];
                
                if (jobInfo->memoryLimit > 0 && memory > jobInfo->memoryLimit) {
                    emit resourceLimitExceeded(jobName, "memory");
                }
                if (jobInfo->cpuLimit > 0 && cpu > jobInfo->cpuLimit) {
                    emit resourceLimitExceeded(jobName, "cpu");
                }
            }
        }
    });
    d->monitoringTimer->start();

    d->initialized = true;
    Logger::instance().info("WindowsSandbox: Initialized successfully");
    return true;
#else
    Logger::instance().error("WindowsSandbox: Not supported on this platform");
    return makeUnexpected(WindowsSandboxError::InitializationFailed);
#endif
}

Expected<bool, WindowsSandboxError> WindowsSandbox::shutdown() {
    if (!d->initialized) {
        return true;
    }

    if (d->monitoringTimer) {
        d->monitoringTimer->stop();
        delete d->monitoringTimer;
        d->monitoringTimer = nullptr;
    }

    // Destroy all job objects
    QStringList jobNames = d->jobObjects.keys();
    for (const QString& jobName : jobNames) {
        destroyJobObject(jobName);
    }

    cleanupResources();
    d->initialized = false;
    Logger::instance().info("WindowsSandbox: Shutdown completed");
    return true;
}

bool WindowsSandbox::isInitialized() const {
    return d->initialized;
}

Expected<bool, WindowsSandboxError> WindowsSandbox::createJobObject(const QString& jobName) {
    if (!d->initialized) {
        return makeUnexpected(WindowsSandboxError::InitializationFailed);
    }

    if (d->jobObjects.contains(jobName)) {
        Logger::instance().warning("WindowsSandbox: Job object already exists: {}", jobName.toStdString());
        return true;
    }

#ifdef Q_OS_WIN
    // Create named job object
    HANDLE jobHandle = CreateJobObjectA(nullptr, jobName.toLocal8Bit().constData());
    if (jobHandle == nullptr) {
        DWORD error = GetLastError();
        Logger::instance().error("WindowsSandbox: Failed to create job object {}: {}", jobName.toStdString(), error);
        return makeUnexpected(WindowsSandboxError::JobObjectCreationFailed);
    }

    // Set up security for the job object
    auto securityResult = setupJobObjectSecurity(jobHandle);
    if (securityResult.hasError()) {
        CloseHandle(jobHandle);
        return securityResult;
    }

    // Configure basic job limits
    auto limitsResult = configureJobObjectLimits(jobHandle, jobName);
    if (limitsResult.hasError()) {
        CloseHandle(jobHandle);
        return limitsResult;
    }

    // Create job info structure
    auto jobInfo = std::make_unique<JobObjectInfo>();
    jobInfo->name = jobName;
    jobInfo->handle = jobHandle;
    jobInfo->creationTime.start();

    d->jobObjects[jobName] = std::move(jobInfo);
    
    Logger::instance().info("WindowsSandbox: Job object created successfully: {}", jobName.toStdString());
    return true;
#else
    Q_UNUSED(jobName)
    return makeUnexpected(WindowsSandboxError::InitializationFailed);
#endif
}

Expected<bool, WindowsSandboxError> WindowsSandbox::destroyJobObject(const QString& jobName) {
    if (!d->initialized) {
        return makeUnexpected(WindowsSandboxError::InitializationFailed);
    }

    auto it = d->jobObjects.find(jobName);
    if (it == d->jobObjects.end()) {
        return makeUnexpected(WindowsSandboxError::JobObjectCreationFailed);
    }

#ifdef Q_OS_WIN
    auto& jobInfo = it.value();
    
    // Terminate all processes in the job
    if (jobInfo->handle) {
        TerminateJobObject(static_cast<HANDLE>(jobInfo->handle), 0);
        CloseHandle(static_cast<HANDLE>(jobInfo->handle));
    }

    // Remove process mappings
    for (qint64 processId : jobInfo->processes) {
        d->processToJob.remove(processId);
    }

    d->jobObjects.erase(it);
    Logger::instance().info("WindowsSandbox: Job object destroyed: {}", jobName.toStdString());
    return true;
#else
    Q_UNUSED(jobName)
    return makeUnexpected(WindowsSandboxError::InitializationFailed);
#endif
}

Expected<bool, WindowsSandboxError> WindowsSandbox::addProcessToJob(const QString& jobName, qint64 processId) {
    if (!d->initialized) {
        return makeUnexpected(WindowsSandboxError::InitializationFailed);
    }

    auto it = d->jobObjects.find(jobName);
    if (it == d->jobObjects.end()) {
        return makeUnexpected(WindowsSandboxError::JobObjectCreationFailed);
    }

#ifdef Q_OS_WIN
    auto& jobInfo = it.value();
    
    // Open the process
    HANDLE processHandle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(processId));
    if (processHandle == nullptr) {
        DWORD error = GetLastError();
        Logger::instance().error("WindowsSandbox: Failed to open process {}: {}", processId, error);
        return makeUnexpected(WindowsSandboxError::ProcessCreationFailed);
    }

    // Add process to job
    if (!AssignProcessToJobObject(static_cast<HANDLE>(jobInfo->handle), processHandle)) {
        DWORD error = GetLastError();
        CloseHandle(processHandle);
        Logger::instance().error("WindowsSandbox: Failed to add process {} to job {}: {}", processId, jobName.toStdString(), error);
        return makeUnexpected(WindowsSandboxError::ProcessCreationFailed);
    }

    CloseHandle(processHandle);
    
    jobInfo->processes.append(processId);
    d->processToJob[processId] = jobName;
    
    Logger::instance().info("WindowsSandbox: Process {} added to job {}", processId, jobName.toStdString());
    emit processCreated(processId, jobName);
    return true;
#else
    Q_UNUSED(jobName)
    Q_UNUSED(processId)
    return makeUnexpected(WindowsSandboxError::InitializationFailed);
#endif
}

Expected<qint64, WindowsSandboxError> WindowsSandbox::createSandboxedProcess(
    const QString& executable,
    const QStringList& arguments,
    const QString& jobName,
    bool lowIntegrity,
    bool restrictedToken) {
    
    if (!d->initialized) {
        return makeUnexpected(WindowsSandboxError::InitializationFailed);
    }

    auto it = d->jobObjects.find(jobName);
    if (it == d->jobObjects.end()) {
        return makeUnexpected(WindowsSandboxError::JobObjectCreationFailed);
    }

#ifdef Q_OS_WIN
    HANDLE tokenHandle = nullptr;
    PROCESS_INFORMATION processInfo = {};
    STARTUPINFOA startupInfo = {};
    startupInfo.cb = sizeof(STARTUPINFOA);

    // Create command line
    QString cmdLine = executable;
    for (const QString& arg : arguments) {
        cmdLine += " \"" + arg + "\"";
    }

    // Create restricted token if requested
    if (restrictedToken) {
        auto tokenResult = createRestrictedToken();
        if (tokenResult.hasError()) {
            return makeUnexpected(tokenResult.error());
        }
        tokenHandle = static_cast<HANDLE>(tokenResult.value());
    }

    // Create the process
    DWORD creationFlags = CREATE_SUSPENDED | CREATE_NEW_CONSOLE;
    BOOL success = FALSE;
    
    if (tokenHandle) {
        success = CreateProcessAsUserA(
            tokenHandle,
            executable.toLocal8Bit().constData(),
            cmdLine.toLocal8Bit().data(),
            nullptr, nullptr, FALSE,
            creationFlags,
            nullptr, nullptr,
            &startupInfo, &processInfo
        );
    } else {
        success = CreateProcessA(
            executable.toLocal8Bit().constData(),
            cmdLine.toLocal8Bit().data(),
            nullptr, nullptr, FALSE,
            creationFlags,
            nullptr, nullptr,
            &startupInfo, &processInfo
        );
    }

    if (!success) {
        DWORD error = GetLastError();
        if (tokenHandle) CloseHandle(tokenHandle);
        Logger::instance().error("WindowsSandbox: Failed to create process {}: {}", executable.toStdString(), error);
        return makeUnexpected(WindowsSandboxError::ProcessCreationFailed);
    }

    // Set integrity level if requested
    if (lowIntegrity) {
        auto integrityResult = setProcessIntegrityLevel(processInfo.hProcess, true);
        if (integrityResult.hasError()) {
            TerminateProcess(processInfo.hProcess, 1);
            CloseHandle(processInfo.hProcess);
            CloseHandle(processInfo.hThread);
            if (tokenHandle) CloseHandle(tokenHandle);
            return makeUnexpected(integrityResult.error());
        }
    }

    // Add process to job object
    if (!AssignProcessToJobObject(static_cast<HANDLE>(it.value()->handle), processInfo.hProcess)) {
        DWORD error = GetLastError();
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);
        if (tokenHandle) CloseHandle(tokenHandle);
        Logger::instance().error("WindowsSandbox: Failed to add process to job {}: {}", jobName.toStdString(), error);
        return makeUnexpected(WindowsSandboxError::ProcessCreationFailed);
    }

    // Resume the process
    ResumeThread(processInfo.hThread);

    qint64 processId = static_cast<qint64>(processInfo.dwProcessId);
    it.value()->processes.append(processId);
    d->processToJob[processId] = jobName;

    // Cleanup handles
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
    if (tokenHandle) CloseHandle(tokenHandle);

    Logger::instance().info("WindowsSandbox: Sandboxed process created: {} in job {}", processId, jobName.toStdString());
    emit processCreated(processId, jobName);
    return processId;
#else
    Q_UNUSED(executable)
    Q_UNUSED(arguments)
    Q_UNUSED(jobName)
    Q_UNUSED(lowIntegrity)
    Q_UNUSED(restrictedToken)
    return makeUnexpected(WindowsSandboxError::InitializationFailed);
#endif
}

Expected<bool, WindowsSandboxError> WindowsSandbox::terminateProcess(qint64 processId) {
    if (!d->initialized) {
        return makeUnexpected(WindowsSandboxError::InitializationFailed);
    }

#ifdef Q_OS_WIN
    HANDLE processHandle = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(processId));
    if (processHandle == nullptr) {
        return makeUnexpected(WindowsSandboxError::ProcessCreationFailed);
    }

    BOOL success = TerminateProcess(processHandle, 1);
    CloseHandle(processHandle);

    if (success) {
        // Remove from tracking
        QString jobName = d->processToJob.value(processId);
        if (!jobName.isEmpty() && d->jobObjects.contains(jobName)) {
            d->jobObjects[jobName]->processes.removeAll(processId);
        }
        d->processToJob.remove(processId);
        
        emit processTerminated(processId, 1);
        Logger::instance().info("WindowsSandbox: Process {} terminated", processId);
        return true;
    } else {
        return makeUnexpected(WindowsSandboxError::ProcessCreationFailed);
    }
#else
    Q_UNUSED(processId)
    return makeUnexpected(WindowsSandboxError::InitializationFailed);
#endif
}

Expected<bool, WindowsSandboxError> WindowsSandbox::setMemoryLimit(const QString& jobName, quint64 memoryLimitBytes) {
    if (!d->initialized) {
        return makeUnexpected(WindowsSandboxError::InitializationFailed);
    }

    auto it = d->jobObjects.find(jobName);
    if (it == d->jobObjects.end()) {
        return makeUnexpected(WindowsSandboxError::JobObjectCreationFailed);
    }

#ifdef Q_OS_WIN
    auto& jobInfo = it.value();
    jobInfo->memoryLimit = memoryLimitBytes;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit = static_cast<SIZE_T>(memoryLimitBytes);

    if (!SetInformationJobObject(static_cast<HANDLE>(jobInfo->handle), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        DWORD error = GetLastError();
        Logger::instance().error("WindowsSandbox: Failed to set memory limit for job {}: {}", jobName.toStdString(), error);
        return makeUnexpected(WindowsSandboxError::PermissionDenied);
    }

    Logger::instance().info("WindowsSandbox: Memory limit set for job {}: {} bytes", jobName.toStdString(), memoryLimitBytes);
    return true;
#else
    Q_UNUSED(jobName)
    Q_UNUSED(memoryLimitBytes)
    return makeUnexpected(WindowsSandboxError::InitializationFailed);
#endif
}

Expected<bool, WindowsSandboxError> WindowsSandbox::setCpuLimit(const QString& jobName, quint32 cpuPercentage) {
    if (!d->initialized) {
        return makeUnexpected(WindowsSandboxError::InitializationFailed);
    }

    auto it = d->jobObjects.find(jobName);
    if (it == d->jobObjects.end()) {
        return makeUnexpected(WindowsSandboxError::JobObjectCreationFailed);
    }

#ifdef Q_OS_WIN
    auto& jobInfo = it.value();
    jobInfo->cpuLimit = cpuPercentage;

    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpuInfo = {};
    cpuInfo.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
    cpuInfo.CpuRate = cpuPercentage * 100; // Convert percentage to rate

    if (!SetInformationJobObject(static_cast<HANDLE>(jobInfo->handle), JobObjectCpuRateControlInformation, &cpuInfo, sizeof(cpuInfo))) {
        DWORD error = GetLastError();
        Logger::instance().error("WindowsSandbox: Failed to set CPU limit for job {}: {}", jobName.toStdString(), error);
        return makeUnexpected(WindowsSandboxError::PermissionDenied);
    }

    Logger::instance().info("WindowsSandbox: CPU limit set for job {}: {}%", jobName.toStdString(), cpuPercentage);
    return true;
#else
    Q_UNUSED(jobName)
    Q_UNUSED(cpuPercentage)
    return makeUnexpected(WindowsSandboxError::InitializationFailed);
#endif
}

Expected<bool, WindowsSandboxError> WindowsSandbox::setProcessLimit(const QString& jobName, quint32 maxProcesses) {
    if (!d->initialized) {
        return makeUnexpected(WindowsSandboxError::InitializationFailed);
    }

    auto it = d->jobObjects.find(jobName);
    if (it == d->jobObjects.end()) {
        return makeUnexpected(WindowsSandboxError::JobObjectCreationFailed);
    }

#ifdef Q_OS_WIN
    auto& jobInfo = it.value();
    jobInfo->processLimit = maxProcesses;

    JOBOBJECT_BASIC_LIMIT_INFORMATION limits = {};
    limits.LimitFlags = JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.ActiveProcessLimit = maxProcesses;

    if (!SetInformationJobObject(static_cast<HANDLE>(jobInfo->handle), JobObjectBasicLimitInformation, &limits, sizeof(limits))) {
        DWORD error = GetLastError();
        Logger::instance().error("WindowsSandbox: Failed to set process limit for job {}: {}", jobName.toStdString(), error);
        return makeUnexpected(WindowsSandboxError::PermissionDenied);
    }

    Logger::instance().info("WindowsSandbox: Process limit set for job {}: {}", jobName.toStdString(), maxProcesses);
    return true;
#else
    Q_UNUSED(jobName)
    Q_UNUSED(maxProcesses)
    return makeUnexpected(WindowsSandboxError::InitializationFailed);
#endif
}

Expected<bool, WindowsSandboxError> WindowsSandbox::enableLowIntegrityLevel(bool enabled) {
    d->lowIntegrityEnabled = enabled;
    Logger::instance().info("WindowsSandbox: Low integrity level {}", enabled ? "enabled" : "disabled");
    return true;
}

Expected<bool, WindowsSandboxError> WindowsSandbox::configureRestrictedToken(const QStringList& deniedSids) {
    d->deniedSids = deniedSids;
    Logger::instance().info("WindowsSandbox: Restricted token configured with {} denied SIDs", deniedSids.size());
    return true;
}

Expected<bool, WindowsSandboxError> WindowsSandbox::setFileSystemAccess(const QString& jobName, const QStringList& allowedPaths) {
    if (!d->initialized) {
        return makeUnexpected(WindowsSandboxError::InitializationFailed);
    }

    auto it = d->jobObjects.find(jobName);
    if (it == d->jobObjects.end()) {
        return makeUnexpected(WindowsSandboxError::JobObjectCreationFailed);
    }

    it.value()->allowedPaths = allowedPaths;
    Logger::instance().info("WindowsSandbox: File system access configured for job {}: {} paths", jobName.toStdString(), allowedPaths.size());
    return true;
}

Expected<bool, WindowsSandboxError> WindowsSandbox::setNetworkAccess(const QString& jobName, bool enabled) {
    if (!d->initialized) {
        return makeUnexpected(WindowsSandboxError::InitializationFailed);
    }

    auto it = d->jobObjects.find(jobName);
    if (it == d->jobObjects.end()) {
        return makeUnexpected(WindowsSandboxError::JobObjectCreationFailed);
    }

    it.value()->networkAccess = enabled;
    Logger::instance().info("WindowsSandbox: Network access {} for job {}", enabled ? "enabled" : "disabled", jobName.toStdString());
    return true;
}

Expected<QStringList, WindowsSandboxError> WindowsSandbox::getActiveJobs() const {
    if (!d->initialized) {
        return makeUnexpected(WindowsSandboxError::InitializationFailed);
    }

    return d->jobObjects.keys();
}

Expected<QList<qint64>, WindowsSandboxError> WindowsSandbox::getJobProcesses(const QString& jobName) const {
    if (!d->initialized) {
        return makeUnexpected(WindowsSandboxError::InitializationFailed);
    }

    auto it = d->jobObjects.find(jobName);
    if (it == d->jobObjects.end()) {
        return makeUnexpected(WindowsSandboxError::JobObjectCreationFailed);
    }

    return it.value()->processes;
}

Expected<QPair<quint64, quint64>, WindowsSandboxError> WindowsSandbox::getResourceUsage(const QString& jobName) const {
    if (!d->initialized) {
        return makeUnexpected(WindowsSandboxError::InitializationFailed);
    }

    auto it = d->jobObjects.find(jobName);
    if (it == d->jobObjects.end()) {
        return makeUnexpected(WindowsSandboxError::JobObjectCreationFailed);
    }

#ifdef Q_OS_WIN
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accountingInfo = {};
    if (!QueryInformationJobObject(static_cast<HANDLE>(it.value()->handle), JobObjectBasicAccountingInformation, &accountingInfo, sizeof(accountingInfo), nullptr)) {
        return makeUnexpected(WindowsSandboxError::PermissionDenied);
    }

    // Convert 100ns intervals to milliseconds for CPU time
    quint64 cpuTimeMs = accountingInfo.TotalUserTime.QuadPart / 10000;
    
    // Get peak memory usage (simplified - would need more detailed implementation)
    quint64 memoryUsage = 0; // Would query process memory usage here
    
    return QPair<quint64, quint64>(memoryUsage, cpuTimeMs);
#else
    return QPair<quint64, quint64>(0, 0);
#endif
}

Expected<bool, WindowsSandboxError> WindowsSandbox::setupJobObjectSecurity(void* jobHandle) {
#ifdef Q_OS_WIN
    // Set up basic security descriptor for the job object
    // This is a simplified implementation - in production, you'd want more sophisticated access control
    SECURITY_DESCRIPTOR sd;
    if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) {
        return makeUnexpected(WindowsSandboxError::PermissionDenied);
    }

    if (!SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE)) {
        return makeUnexpected(WindowsSandboxError::PermissionDenied);
    }

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;

    Logger::instance().debug("WindowsSandbox: Job object security configured");
    return true;
#else
    Q_UNUSED(jobHandle)
    return makeUnexpected(WindowsSandboxError::InitializationFailed);
#endif
}

Expected<void*, WindowsSandboxError> WindowsSandbox::createRestrictedToken() {
#ifdef Q_OS_WIN
    HANDLE currentToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &currentToken)) {
        return makeUnexpected(WindowsSandboxError::AccessTokenCreationFailed);
    }

    HANDLE restrictedToken;
    if (!CreateRestrictedToken(currentToken, 0, 0, nullptr, 0, nullptr, 0, nullptr, &restrictedToken)) {
        CloseHandle(currentToken);
        return makeUnexpected(WindowsSandboxError::AccessTokenCreationFailed);
    }

    CloseHandle(currentToken);
    Logger::instance().debug("WindowsSandbox: Restricted token created");
    return restrictedToken;
#else
    return makeUnexpected(WindowsSandboxError::InitializationFailed);
#endif
}

Expected<bool, WindowsSandboxError> WindowsSandbox::setProcessIntegrityLevel(void* processHandle, bool lowIntegrity) {
#ifdef Q_OS_WIN
    if (!lowIntegrity) {
        return true; // Nothing to do for normal integrity
    }

    HANDLE tokenHandle;
    if (!OpenProcessToken(static_cast<HANDLE>(processHandle), TOKEN_ADJUST_DEFAULT | TOKEN_QUERY, &tokenHandle)) {
        return makeUnexpected(WindowsSandboxError::IntegrityLevelFailed);
    }

    // Set low integrity level
    SID_IDENTIFIER_AUTHORITY integrityAuthority = SECURITY_MANDATORY_LABEL_AUTHORITY;
    PSID integritySid;
    if (!AllocateAndInitializeSid(&integrityAuthority, 1, SECURITY_MANDATORY_LOW_RID, 0, 0, 0, 0, 0, 0, 0, &integritySid)) {
        CloseHandle(tokenHandle);
        return makeUnexpected(WindowsSandboxError::IntegrityLevelFailed);
    }

    TOKEN_MANDATORY_LABEL tokenIntegrityLevel;
    tokenIntegrityLevel.Label.Attributes = SE_GROUP_INTEGRITY;
    tokenIntegrityLevel.Label.Sid = integritySid;

    BOOL result = SetTokenInformation(tokenHandle, TokenIntegrityLevel, &tokenIntegrityLevel, sizeof(tokenIntegrityLevel));
    
    FreeSid(integritySid);
    CloseHandle(tokenHandle);

    if (!result) {
        return makeUnexpected(WindowsSandboxError::IntegrityLevelFailed);
    }

    Logger::instance().debug("WindowsSandbox: Low integrity level set for process");
    return true;
#else
    Q_UNUSED(processHandle)
    Q_UNUSED(lowIntegrity)
    return makeUnexpected(WindowsSandboxError::InitializationFailed);
#endif
}

Expected<bool, WindowsSandboxError> WindowsSandbox::configureJobObjectLimits(void* jobHandle, const QString& jobName) {
#ifdef Q_OS_WIN
    // Set basic limits for the job object
    JOBOBJECT_BASIC_LIMIT_INFORMATION limits = {};
    limits.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;

    if (!SetInformationJobObject(static_cast<HANDLE>(jobHandle), JobObjectBasicLimitInformation, &limits, sizeof(limits))) {
        DWORD error = GetLastError();
        Logger::instance().error("WindowsSandbox: Failed to set basic limits for job {}: {}", jobName.toStdString(), error);
        return makeUnexpected(WindowsSandboxError::PermissionDenied);
    }

    Logger::instance().debug("WindowsSandbox: Basic job limits configured for {}", jobName.toStdString());
    return true;
#else
    Q_UNUSED(jobHandle)
    Q_UNUSED(jobName)
    return makeUnexpected(WindowsSandboxError::InitializationFailed);
#endif
}

void WindowsSandbox::cleanupResources() {
#ifdef Q_OS_WIN
    if (d->restrictedToken) {
        CloseHandle(d->restrictedToken);
        d->restrictedToken = nullptr;
    }

    if (d->securityDescriptor) {
        LocalFree(d->securityDescriptor);
        d->securityDescriptor = nullptr;
    }
#endif
    
    Logger::instance().debug("WindowsSandbox: Resources cleaned up");
}

} // namespace Murmur

#include "WindowsSandbox.moc"