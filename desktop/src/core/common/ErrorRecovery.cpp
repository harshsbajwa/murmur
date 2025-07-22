#include "ErrorRecovery.hpp"
#include "Logger.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QMetaEnum>
#include <QtCore/QDebug>

namespace Murmur {

// Helper function to convert ErrorSeverity to a string, as Q_ENUM might be missing.
namespace {
const char* severityToString(ErrorSeverity severity) {
    switch (severity) {
        case ErrorSeverity::Info:     return "Info";
        case ErrorSeverity::Warning:  return "Warning";
        case ErrorSeverity::Error:    return "Error";
        case ErrorSeverity::Critical: return "Critical";
        case ErrorSeverity::Fatal:    return "Fatal";
        default:                      return "Unknown";
    }
}
} // anonymous namespace

ErrorRecovery::ErrorRecovery(QObject* parent)
    : QObject(parent)
    , retryManager_(std::make_unique<RetryManager>(this)) {
    
    // Register default recovery strategies for different error severities
    registerGlobalStrategy(ErrorSeverity::Error, RecoveryStrategies::retryWithExponentialBackoff(3));
    registerGlobalStrategy(ErrorSeverity::Critical, RecoveryStrategies::userPrompt("Critical error occurred"));
    registerGlobalStrategy(ErrorSeverity::Fatal, RecoveryStrategies::terminate("Fatal error - application must terminate"));
    
    Logger::instance().info("Error recovery system initialized");
}

void ErrorRecovery::reportError(const ErrorContext& context) {
    if (!errorReportingEnabled_) {
        return;
    }
    
    // Add to error history
    ErrorContext contextCopy = context;
    contextCopy.timestamp = QDateTime::currentDateTime();
    
    errorHistory_.append(contextCopy);
    
    // Trim history if needed
    while (errorHistory_.size() > maxErrorHistory_) {
        errorHistory_.removeFirst();
    }
    
    // Update circuit breaker
    updateCircuitBreaker(context.component, false);
    
    // Log the error
    QString logLevel;
    switch (context.severity) {
        case ErrorSeverity::Info:
            logLevel = "INFO";
            Logger::instance().info("{}: {}", context.component.toStdString(), context.toString().toStdString());
            break;
        case ErrorSeverity::Warning:
            logLevel = "WARNING";
            Logger::instance().warn("{}: {}", context.component.toStdString(), context.toString().toStdString());
            break;
        case ErrorSeverity::Error:
            logLevel = "ERROR";
            Logger::instance().error("{}: {}", context.component.toStdString(), context.toString().toStdString());
            break;
        case ErrorSeverity::Critical:
            logLevel = "CRITICAL";
            Logger::instance().error("{}: CRITICAL: {}", context.component.toStdString(), context.toString().toStdString());
            break;
        case ErrorSeverity::Fatal:
            logLevel = "FATAL";
            Logger::instance().error("{}: FATAL: {}", context.component.toStdString(), context.toString().toStdString());
            break;
    }
    
    emit errorReported(contextCopy);
    
    // Attempt automatic recovery if enabled
    if (autoRecoveryEnabled_ && context.severity >= ErrorSeverity::Error) {
        attemptRecoveryAsync(contextCopy, [](bool success, const QString& message) {
            if (success) {
                Logger::instance().info("Automatic recovery succeeded: {}", message.toStdString());
            } else {
                Logger::instance().warn("Automatic recovery failed: {}", message.toStdString());
            }
        });
    }
}

void ErrorRecovery::reportError(const QString& component, const QString& operation, 
                               const QString& error, ErrorSeverity severity) {
    ErrorContext context;
    context.component = component;
    context.operation = operation;
    context.errorMessage = error;
    context.severity = severity;
    context.timestamp = QDateTime::currentDateTime();
    
    reportError(context);
}

void ErrorRecovery::registerRecoveryStrategy(const QString& component, const QString& operation, 
                                             const RecoveryStrategy& strategy) {
    componentStrategies_[component][operation] = strategy;
    Logger::instance().info("ErrorRecovery: Registered recovery strategy for {}::{} - {}",
                component.toStdString(), operation.toStdString(), strategy.description.toStdString());
}

void ErrorRecovery::registerGlobalStrategy(ErrorSeverity severity, const RecoveryStrategy& strategy) {
    globalStrategies_[severity] = strategy;
    Logger::instance().info("ErrorRecovery: Registered global recovery strategy for {} - {}",
                severityToString(severity), strategy.description.toStdString());
}

Expected<bool, QString> ErrorRecovery::attemptRecovery(const ErrorContext& context) {
    if (isCircuitOpen(context.component)) {
        return makeUnexpected(QString("Circuit breaker is open for component: %1").arg(context.component));
    }
    
    RecoveryStrategy strategy = findRecoveryStrategy(context);
    if (strategy.primaryAction == RecoveryAction::None) {
        return makeUnexpected("No recovery strategy found");
    }
    
    Logger::instance().info("ErrorRecovery: Attempting recovery for {}::{} using strategy: {}",
                context.component.toStdString(), context.operation.toStdString(), strategy.description.toStdString());
    
    // Execute primary recovery action
    bool success = executeRecoveryAction(strategy.primaryAction, context, strategy);
    
    if (!success && strategy.fallbackAction != RecoveryAction::None) {
        Logger::instance().info("Primary recovery failed, attempting fallback");
        success = executeRecoveryAction(strategy.fallbackAction, context, strategy);
    }
    
    // Update circuit breaker based on recovery result
    updateCircuitBreaker(context.component, success);
    
    emit recoveryAttempted(context.component, context.operation, success);
    
    if (success) {
        return true;
    } else {
        return makeUnexpected("Recovery attempts failed");
    }
}

void ErrorRecovery::attemptRecoveryAsync(const ErrorContext& context, 
                                        std::function<void(bool, const QString&)> callback) {
    auto result = attemptRecovery(context);
    if (callback) {
        if (result.hasValue()) {
            callback(result.value(), "Recovery successful");
        } else {
            callback(false, result.error());
        }
    }
}

void ErrorRecovery::enableCircuitBreaker(const QString& component, int failureThreshold, 
                                        std::chrono::milliseconds resetTimeout) {
    CircuitBreakerState& state = circuitBreakers_[component];
    state.failureThreshold = failureThreshold;
    state.resetTimeout = resetTimeout;
    state.isOpen = false;
    state.failureCount = 0;
    
    Logger::instance().info("ErrorRecovery: Circuit breaker enabled for {} (threshold: {}, timeout: {}ms)",
                component.toStdString(), failureThreshold, resetTimeout.count());
}

bool ErrorRecovery::isCircuitOpen(const QString& component) const {
    auto it = circuitBreakers_.find(component);
    if (it == circuitBreakers_.end()) {
        return false;
    }
    
    const CircuitBreakerState& state = it.value();
    if (!state.isOpen) {
        return false;
    }
    
    // Check if reset timeout has passed
    auto now = QDateTime::currentDateTime();
    auto timeSinceFailure = state.lastFailure.msecsTo(now);
    
    if (timeSinceFailure >= state.resetTimeout.count()) {
        // Time to try resetting the circuit
        const_cast<CircuitBreakerState&>(state).isOpen = false;
        const_cast<CircuitBreakerState&>(state).failureCount = 0;
        Logger::instance().info("ErrorRecovery: Circuit breaker reset for component: {}", component.toStdString());
        const_cast<ErrorRecovery*>(this)->circuitBreakerReset(component);
        return false;
    }
    
    return true;
}

void ErrorRecovery::resetCircuit(const QString& component) {
    auto it = circuitBreakers_.find(component);
    if (it != circuitBreakers_.end()) {
        it.value().isOpen = false;
        it.value().failureCount = 0;
        Logger::instance().info("ErrorRecovery: Circuit breaker manually reset for component: {}", component.toStdString());
        emit circuitBreakerReset(component);
    }
}

void ErrorRecovery::startHealthCheck(const QString& component, 
                                    std::function<bool()> healthCheck,
                                    std::chrono::milliseconds interval) {
    stopHealthCheck(component); // Stop any existing health check
    
    HealthCheckConfig config;
    config.healthCheck = healthCheck;
    config.timer = new QTimer(this);
    config.interval = interval;
    config.lastResult = true;
    
    connect(config.timer, &QTimer::timeout, this, &ErrorRecovery::performHealthCheck);
    config.timer->start(interval.count());
    
    healthChecks_[component] = config;
    
    Logger::instance().info("ErrorRecovery: Health check started for {} (interval: {}ms)",
                component.toStdString(), interval.count());
}

void ErrorRecovery::stopHealthCheck(const QString& component) {
    auto it = healthChecks_.find(component);
    if (it != healthChecks_.end()) {
        it.value().timer->stop();
        it.value().timer->deleteLater();
        healthChecks_.erase(it);
        Logger::instance().info("ErrorRecovery: Health check stopped for component: {}", component.toStdString());
    }
}

bool ErrorRecovery::isComponentHealthy(const QString& component) const {
    auto it = healthChecks_.find(component);
    if (it == healthChecks_.end()) {
        return true; // Assume healthy if no health check configured
    }
    
    return it.value().lastResult;
}

void ErrorRecovery::setMaxErrorHistory(int maxErrors) {
    maxErrorHistory_ = maxErrors;
    
    // Trim current history if needed
    while (errorHistory_.size() > maxErrorHistory_) {
        errorHistory_.removeFirst();
    }
}

void ErrorRecovery::setErrorReportingEnabled(bool enabled) {
    errorReportingEnabled_ = enabled;
    Logger::instance().info("ErrorRecovery: Error reporting {}", enabled ? "enabled" : "disabled");
}

void ErrorRecovery::setAutoRecoveryEnabled(bool enabled) {
    autoRecoveryEnabled_ = enabled;
    Logger::instance().info("ErrorRecovery: Auto recovery {}", enabled ? "enabled" : "disabled");
}

QList<ErrorContext> ErrorRecovery::getErrorHistory(const QString& component) const {
    if (component.isEmpty()) {
        return errorHistory_;
    }
    
    QList<ErrorContext> filtered;
    for (const auto& error : errorHistory_) {
        if (error.component == component) {
            filtered.append(error);
        }
    }
    return filtered;
}

QVariantMap ErrorRecovery::getErrorStatistics(const QString& component) const {
    QVariantMap stats;
    
    QList<ErrorContext> errors = getErrorHistory(component);
    
    stats["totalErrors"] = errors.size();
    stats["component"] = component.isEmpty() ? "All Components" : component;
    
    // Count by severity
    QMap<ErrorSeverity, int> severityCounts;
    QMap<QString, int> operationCounts;
    
    for (const auto& error : errors) {
        severityCounts[error.severity]++;
        operationCounts[error.operation]++;
    }
    
    QVariantMap severityStats;
    for (auto it = severityCounts.begin(); it != severityCounts.end(); ++it) {
        QString severityName(severityToString(it.key()));
        severityStats[severityName] = it.value();
    }
    stats["bySeverity"] = severityStats;
    
    QVariantMap operationStats;
    for (auto it = operationCounts.begin(); it != operationCounts.end(); ++it) {
        operationStats[it.key()] = it.value();
    }
    stats["byOperation"] = operationStats;
    
    // Recent error rate (last hour)
    QDateTime oneHourAgo = QDateTime::currentDateTime().addSecs(-3600);
    int recentErrors = 0;
    for (const auto& error : errors) {
        if (error.timestamp > oneHourAgo) {
            recentErrors++;
        }
    }
    stats["recentErrorsLastHour"] = recentErrors;
    
    return stats;
}

void ErrorRecovery::clearErrorHistory(const QString& component) {
    if (component.isEmpty()) {
        errorHistory_.clear();
        Logger::instance().info("All error history cleared");
    } else {
        errorHistory_.erase(
            std::remove_if(errorHistory_.begin(), errorHistory_.end(),
                          [&component](const ErrorContext& error) {
                              return error.component == component;
                          }),
            errorHistory_.end()
        );
        Logger::instance().info("ErrorRecovery: Error history cleared for component: {}", component.toStdString());
    }
}

void ErrorRecovery::performHealthCheck() {
    QTimer* timer = qobject_cast<QTimer*>(sender());
    if (!timer) {
        return;
    }
    
    // Find which component this timer belongs to
    QString component;
    for (auto it = healthChecks_.begin(); it != healthChecks_.end(); ++it) {
        if (it.value().timer == timer) {
            component = it.key();
            break;
        }
    }
    
    if (component.isEmpty()) {
        return;
    }
    
    HealthCheckConfig& config = healthChecks_[component];
    bool currentResult = config.healthCheck();
    
    if (currentResult != config.lastResult) {
        config.lastResult = currentResult;
        emit componentHealthChanged(component, currentResult);
        
        if (currentResult) {
            Logger::instance().info("ErrorRecovery: Component {} health restored", component.toStdString());
            resetCircuit(component); // Reset circuit breaker on health recovery
        } else {
            Logger::instance().warn("Component {} health check failed", component.toStdString());
        }
    }
}

RecoveryStrategy ErrorRecovery::findRecoveryStrategy(const ErrorContext& context) const {
    // Check for component-specific strategy first
    auto componentIt = componentStrategies_.find(context.component);
    if (componentIt != componentStrategies_.end()) {
        auto operationIt = componentIt.value().find(context.operation);
        if (operationIt != componentIt.value().end()) {
            return operationIt.value();
        }
    }
    
    // Fall back to global strategy based on severity
    auto globalIt = globalStrategies_.find(context.severity);
    if (globalIt != globalStrategies_.end()) {
        return globalIt.value();
    }
    
    // Return empty strategy if nothing found
    return RecoveryStrategy{};
}

bool ErrorRecovery::executeRecoveryAction(RecoveryAction action, const ErrorContext& context, 
                                         const RecoveryStrategy& strategy) {
    switch (action) {
        case RecoveryAction::None:
            return true;
            
        case RecoveryAction::Retry:
            if (strategy.recovery) {
                // Use custom retry logic
                return strategy.recovery();
            }
            // Use default retry mechanism
            return performRetryOperation(context, strategy);
            
        case RecoveryAction::Fallback:
            if (strategy.fallback) {
                return strategy.fallback();
            }
            return false;
            
        case RecoveryAction::Reset:
            if (strategy.recovery) {
                return strategy.recovery();
            }
            return false;
            
        case RecoveryAction::Restart:
            return performComponentRestart(context);
            
        case RecoveryAction::UserPrompt:
            // User prompt logic - emit signal for UI to handle
            Logger::instance().info("ErrorRecovery: User intervention for {}::{} required: {}",
                                       context.component.toStdString(),
                                       context.operation.toStdString(),
                                       strategy.description.toStdString());
            
            // Emit signal for UI handling
            emit userPromptRequested(context.component, context.operation, 
                                   strategy.description, context.errorMessage);
            
            // Store context for user response
            pendingUserPrompts_[context.component + "::" + context.operation] = context;
            
            return false; // Wait for user response
            
        case RecoveryAction::Terminate:
            Logger::instance().error("Terminating component {} due to: {}",
                                        context.component.toStdString(),
                                        strategy.description.toStdString());
            QCoreApplication::quit();
            return false;
    }
    
    return false;
}

void ErrorRecovery::updateCircuitBreaker(const QString& component, bool success) {
    auto it = circuitBreakers_.find(component);
    if (it == circuitBreakers_.end()) {
        return;
    }
    
    CircuitBreakerState& state = it.value();
    
    if (success) {
        state.failureCount = 0;
        if (state.isOpen) {
            state.isOpen = false;
            emit circuitBreakerReset(component);
            Logger::instance().info("ErrorRecovery: Circuit breaker reset for component: {}", component.toStdString());
        }
    } else {
        state.failureCount++;
        state.lastFailure = QDateTime::currentDateTime();
        
        if (state.failureCount >= state.failureThreshold && !state.isOpen) {
            state.isOpen = true;
            emit circuitBreakerTripped(component);
            Logger::instance().warn("Circuit breaker tripped for component: {} (failures: {})",
                           component.toStdString(), state.failureCount);
        }
    }
}

bool ErrorRecovery::performComponentRestart(const ErrorContext& context) {
    Logger::instance().info("ErrorRecovery: Attempting to restart component: {}", context.component.toStdString());
    
    // Component restart strategy based on component type
    if (context.component == "FFmpegWrapper") {
        // For FFmpeg, we can restart by reinitializing the wrapper
        emit componentRestartRequested(context.component, "reinitialize_libraries");
        return true;
    }
    else if (context.component == "WhisperEngine") {
        // For Whisper, restart by unloading and reloading models
        emit componentRestartRequested(context.component, "reload_models");
        return true;
    }
    else if (context.component == "TorrentEngine") {
        // For torrent engine, restart session
        emit componentRestartRequested(context.component, "restart_session");
        return true;
    }
    else if (context.component == "VideoPlayer") {
        // For video player, stop and reset
        emit componentRestartRequested(context.component, "stop_and_reset");
        return true;
    }
    else if (context.component == "StorageManager") {
        // For storage manager, close and reopen connections
        emit componentRestartRequested(context.component, "reconnect_database");
        return true;
    }
    else if (context.component.startsWith("Network")) {
        // For network components, reset connections
        emit componentRestartRequested(context.component, "reset_connections");
        return true;
    }
    else {
        // Generic restart strategy - emit signal for component to handle
        Logger::instance().warn("Generic restart requested for component: {}", context.component.toStdString());
        emit componentRestartRequested(context.component, "generic_restart");
        
        // Store restart request for async confirmation
        QString key = context.component + "::restart";
        pendingUserPrompts_[key] = context;
        
        // Return false to indicate async handling (confirmation needed)
        return false;
    }
}

bool ErrorRecovery::performRetryOperation(const ErrorContext& context, const RecoveryStrategy& strategy) {
    if (!retryManager_) {
        Logger::instance().warn("RetryManager not available for retry operation");
        return false;
    }
    
    // Configure retry policy based on context
    RetryConfig config = strategy.retryConfig;
    if (config.maxAttempts <= 0) {
        config.maxAttempts = 3; // Default retry attempts
    }
    
    // Choose retry policy based on component and error type
    if (context.component.contains("Network") || context.component.contains("Download")) {
        config.policy = RetryPolicy::Exponential;
        config.initialDelay = std::chrono::milliseconds(1000);
        config.maxDelay = std::chrono::milliseconds(30000);
        config.backoffMultiplier = 2.0;
    } else if (context.component.contains("Storage") || context.component.contains("Database")) {
        config.policy = RetryPolicy::Linear;
        config.initialDelay = std::chrono::milliseconds(500);
        config.maxDelay = std::chrono::milliseconds(5000);
    } else if (context.component.contains("FFmpeg") || context.component.contains("Media")) {
        config.policy = RetryPolicy::Fibonacci;
        config.initialDelay = std::chrono::milliseconds(2000);
        config.maxDelay = std::chrono::milliseconds(15000);
    } else {
        // Default policy for other components
        config.policy = RetryPolicy::Exponential;
        config.initialDelay = std::chrono::milliseconds(1000);
        config.maxDelay = std::chrono::milliseconds(10000);
    }
    
    // Add jitter to avoid thundering herd
    config.enableJitter = true;
    config.jitterFactor = 0.1;
    
    // Set up custom retry condition based on error severity and type
    config.shouldRetry = [context](int /*attempt*/, const QString& error) -> bool {
        // Don't retry fatal errors
        if (context.severity == ErrorSeverity::Fatal) {
            return false;
        }
        
        // Don't retry certain error types
        if (error.contains("permission", Qt::CaseInsensitive) ||
            error.contains("access denied", Qt::CaseInsensitive) ||
            error.contains("unauthorized", Qt::CaseInsensitive) ||
            error.contains("invalid credentials", Qt::CaseInsensitive)) {
            return false;
        }
        
        // Always retry network and I/O errors
        if (error.contains("network", Qt::CaseInsensitive) ||
            error.contains("connection", Qt::CaseInsensitive) ||
            error.contains("timeout", Qt::CaseInsensitive) ||
            error.contains("i/o", Qt::CaseInsensitive)) {
            return true;
        }
        
        // Retry most other errors for up to max attempts
        return true;
    };
    
    retryManager_->setConfig(config);
    
    // Create retry operation that emits recovery signals
    auto retryOperation = [this, context]() -> Expected<bool, QString> {
        Logger::instance().info("Attempting recovery for {}::{} - {}", 
                               context.component.toStdString(),
                               context.operation.toStdString(),
                               context.errorMessage.toStdString());
        
        // Emit signal for component to retry operation
        emit retryRequested(context.component, context.operation, context.errorMessage);
        
        // Store retry request for async feedback
        QString key = context.component + "::" + context.operation + "::retry";
        pendingUserPrompts_[key] = context;
        
        // Return false to indicate async handling (feedback needed)
        return false;
    };
    
    // Define retry failure callback
    auto isRetryable = [](const QString& error) -> bool {
        return !error.contains("permission", Qt::CaseInsensitive) &&
               !error.contains("access denied", Qt::CaseInsensitive);
    };
    
    // Execute retry operation
    auto result = retryManager_->execute<bool, QString>(retryOperation, isRetryable);
    
    if (result) {
        Logger::instance().info("Retry operation succeeded for {}::{}", 
                               context.component.toStdString(),
                               context.operation.toStdString());
        return true;
    } else {
        Logger::instance().error("Retry operation failed for {}::{}: {}", 
                                context.component.toStdString(),
                                context.operation.toStdString(),
                                static_cast<int>(result.error()));
        return false;
    }
}

void ErrorRecovery::handleUserResponse(const QString& component, const QString& operation, bool shouldRetry) {
    QString key = component + "::" + operation;
    
    if (pendingUserPrompts_.contains(key)) {
        ErrorContext context = pendingUserPrompts_[key];
        pendingUserPrompts_.remove(key);
        
        Logger::instance().info("User response received for {}::{}, retry: {}", 
                               component.toStdString(),
                               operation.toStdString(),
                               shouldRetry);
        
        if (shouldRetry) {
            // Attempt recovery again
            attemptRecovery(context);
        } else {
            // User chose not to retry, emit recovery attempted with failure
            emit recoveryAttempted(component, operation, false);
        }
    } else {
        Logger::instance().warn("Received user response for unknown prompt: {}::{}", 
                               component.toStdString(),
                               operation.toStdString());
    }
}

} // namespace Murmur