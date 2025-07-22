#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QDir>
#include <memory>
#include <functional>
#include <unordered_map>

#include "core/common/Expected.hpp"
#include "core/common/Logger.hpp"

namespace Murmur {

enum class SandboxError {
    InitializationFailed,
    ViolationDetected,
    RestrictedOperation,
    InvalidPath,
    ProcessCreationFailed,
    PermissionDenied,
    ConfigurationError,
    NetworkRestricted,
    ExecutionBlocked,
    SandboxNotFound,
    FeatureDisabled
};

enum class SandboxPermission {
    ReadFile,
    WriteFile,
    CreateFile,
    DeleteFile,
    ExecuteFile,
    NetworkAccess,
    SystemCall,
    ProcessCreation
};

struct ResourceUsageInfo {
    int memoryUsage = 0;
    int cpuTime = 0;
    qint64 timestamp = 0; // When this usage was recorded
    bool isDestroyed = false; // Whether sandbox was destroyed
};

struct SandboxConfig {
    QStringList allowedPaths;
    QStringList deniedPaths;
    QStringList allowedExecutables;
    QStringList allowedNetworkDomains;
    QList<SandboxPermission> permissions;
    bool enableNetworkAccess = false;
    bool enableSystemCalls = false;
    bool enableProcessCreation = false;
    int maxMemoryUsage = 1024 * 1024 * 512; // 512MB default
    int maxCpuTime = 60; // 60 seconds default
    bool enableResourceUsageCache = false; // Feature flag for caching resource usage after destruction
};

class SandboxManager : public QObject {
    Q_OBJECT

public:
    explicit SandboxManager(QObject* parent = nullptr);
    ~SandboxManager() override;

    // Initialization
    Expected<void, SandboxError> initialize(const SandboxConfig& config);
    Expected<void, SandboxError> shutdown();
    bool isInitialized() const;

    // Sandbox operations
    Expected<void, SandboxError> createSandbox(const QString& sandboxId, const SandboxConfig& config);
    Expected<void, SandboxError> destroySandbox(const QString& sandboxId);
    Expected<void, SandboxError> enterSandbox(const QString& sandboxId);
    Expected<void, SandboxError> exitSandbox(const QString& sandboxId);

    // Permission checks
    Expected<bool, SandboxError> checkPermission(const QString& sandboxId, SandboxPermission permission);
    Expected<bool, SandboxError> checkPathAccess(const QString& sandboxId, const QString& path, SandboxPermission permission);
    Expected<bool, SandboxError> checkNetworkAccess(const QString& sandboxId, const QString& domain, int port);
    Expected<void, SandboxError> requestNetworkAccess(const QString& domain, int port);

    // Secure operations
    Expected<void, SandboxError> executeInSandbox(const QString& sandboxId, const QString& executable, const QStringList& arguments);
    Expected<QByteArray, SandboxError> readFileInSandbox(const QString& sandboxId, const QString& filePath);
    Expected<void, SandboxError> writeFileInSandbox(const QString& sandboxId, const QString& filePath, const QByteArray& data);
    Expected<void, SandboxError> executeCommand(const QString& command, const QStringList& args);
    
    // Privilege management (for test compatibility)
    Expected<QStringList, SandboxError> getCurrentPrivileges() const;
    bool hasAdministratorPrivileges() const;
    Expected<void, SandboxError> requestPrivilegeElevation();
    Expected<void, SandboxError> requestFileAccess(const QString& path, const QString& mode);

    // Configuration
    Expected<void, SandboxError> updateSandboxConfig(const QString& sandboxId, const SandboxConfig& config);
    Expected<SandboxConfig, SandboxError> getSandboxConfig(const QString& sandboxId);

    // Monitoring
    Expected<void, SandboxError> enableMonitoring(const QString& sandboxId, bool enable);
    Expected<QStringList, SandboxError> getViolations(const QString& sandboxId);
    Expected<void, SandboxError> clearViolations(const QString& sandboxId);

    // Resource management
    Expected<void, SandboxError> setResourceLimits(const QString& sandboxId, int maxMemory, int maxCpuTime);
    
    /**
     * @brief Get current resource usage for a sandbox
     * @param sandboxId The ID of the sandbox to query
     * @return QPair<memoryUsage, cpuTime> on success, error on failure
     * 
     * Behavior:
     * - For active sandboxes: Returns current live resource usage
     * - For destroyed sandboxes with cache enabled: Returns last known usage before destruction
     * - For destroyed sandboxes with cache disabled: Returns SandboxError::SandboxNotFound
     * - For non-existent sandboxes: Returns SandboxError::SandboxNotFound
     * - For uninitialized SandboxManager: Returns SandboxError::InitializationFailed
     */
    Expected<QPair<int, int>, SandboxError> getResourceUsage(const QString& sandboxId);
    
    /**
     * @brief Get detailed resource usage information including metadata
     * @param sandboxId The ID of the sandbox to query
     * @return ResourceUsageInfo containing usage data and metadata
     * 
     * This method provides additional information such as when the usage was recorded
     * and whether the sandbox has been destroyed. Requires cache to be enabled for
     * destroyed sandboxes.
     */
    Expected<ResourceUsageInfo, SandboxError> getDetailedResourceUsage(const QString& sandboxId);
    
    /**
     * @brief Enable or disable resource usage caching
     * @param enable True to enable caching, false to disable
     * 
     * When enabled, resource usage is cached when sandboxes are destroyed,
     * allowing queries after destruction. When disabled, resource queries
     * for destroyed sandboxes will fail.
     */
    void setResourceUsageCacheEnabled(bool enable);
    
    /**
     * @brief Check if resource usage caching is enabled
     * @return True if caching is enabled, false otherwise
     */
    bool isResourceUsageCacheEnabled() const;
    
    /**
     * @brief Clear cached resource usage data
     * @param sandboxId Optional specific sandbox ID to clear. If empty, clears all cached data.
     */
    void clearResourceUsageCache(const QString& sandboxId = QString());

signals:
    void sandboxCreated(const QString& sandboxId);
    void sandboxDestroyed(const QString& sandboxId);
    void violationDetected(const QString& sandboxId, const QString& violation);
    void resourceLimitExceeded(const QString& sandboxId, const QString& resource);

private:
    class SandboxManagerPrivate;
    std::unique_ptr<SandboxManagerPrivate> d;

    // Helper methods
    Expected<void, SandboxError> validateConfig(const SandboxConfig& config);
    Expected<void, SandboxError> validatePath(const QString& path);
    Expected<void, SandboxError> logViolation(const QString& sandboxId, const QString& violation);
    Expected<void, SandboxError> enforceResourceLimits(const QString& sandboxId);
    
    // Resource cache helpers
    void cacheResourceUsage(const QString& sandboxId, const ResourceUsageInfo& usage);
    void updateResourceUsageTimestamp(const QString& sandboxId);
};

} // namespace Murmur