#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QMap>
#include <QtCore/QDateTime>
#include <QtCore/QMetaEnum>
#include <functional>
#include <memory>
#include "Expected.hpp"
#include "RetryManager.hpp"

namespace Murmur {

enum class ErrorSeverity {
    Info,       // Informational, no action needed
    Warning,    // Warning, operation can continue
    Error,      // Error, operation should be retried
    Critical,   // Critical error, user intervention required
    Fatal       // Fatal error, application should terminate
};

enum class RecoveryAction {
    None,           // No recovery action
    Retry,          // Retry the operation
    Fallback,       // Use fallback mechanism
    Reset,          // Reset component state
    Restart,        // Restart component
    UserPrompt,     // Ask user for guidance
    Terminate       // Terminate operation/application
};

struct ErrorContext {
    QString component;
    QString operation;
    QString errorMessage;
    QString errorCode;
    ErrorSeverity severity;
    QDateTime timestamp;
    QStringList stackTrace;
    QVariantMap metadata;
    
    QString toString() const {
        QString severityStr;
        switch (severity) {
            case ErrorSeverity::Info: severityStr = "Info"; break;
            case ErrorSeverity::Warning: severityStr = "Warning"; break;
            case ErrorSeverity::Error: severityStr = "Error"; break;
            case ErrorSeverity::Critical: severityStr = "Critical"; break;
            case ErrorSeverity::Fatal: severityStr = "Fatal"; break;
        }
        return QString("[%1] %2::%3 - %4 (%5)")
               .arg(severityStr)
               .arg(component)
               .arg(operation)
               .arg(errorMessage)
               .arg(errorCode);
    }
};

struct RecoveryStrategy {
    RecoveryAction primaryAction = RecoveryAction::None;
    RecoveryAction fallbackAction = RecoveryAction::None;
    std::function<bool()> condition = nullptr;
    std::function<bool()> recovery = nullptr;
    std::function<bool()> fallback = nullptr;
    RetryConfig retryConfig;
    QString description;
    int maxRecoveryAttempts = 3;
    std::chrono::milliseconds cooldownPeriod{5000};
};

/**
 * @brief Comprehensive error recovery and resilience framework
 * 
 * Provides automated error recovery, fallback mechanisms, and graceful degradation
 * for robust application behavior under various failure conditions.
 */
class ErrorRecovery : public QObject {
    Q_OBJECT
    Q_ENUM(ErrorSeverity)
    Q_ENUM(RecoveryAction)

public:
    explicit ErrorRecovery(QObject* parent = nullptr);

    // Error reporting and handling
    void reportError(const ErrorContext& context);
    void reportError(const QString& component, const QString& operation, 
                    const QString& error, ErrorSeverity severity = ErrorSeverity::Error);

    // Recovery strategy registration
    void registerRecoveryStrategy(const QString& component, const QString& operation, 
                                 const RecoveryStrategy& strategy);
    void registerGlobalStrategy(ErrorSeverity severity, const RecoveryStrategy& strategy);

    // Recovery execution
    Expected<bool, QString> attemptRecovery(const ErrorContext& context);
    void attemptRecoveryAsync(const ErrorContext& context, 
                             std::function<void(bool, const QString&)> callback);
    
    // User prompt handling
    void handleUserResponse(const QString& component, const QString& operation, bool shouldRetry);

    // Circuit breaker pattern
    void enableCircuitBreaker(const QString& component, int failureThreshold = 5, 
                             std::chrono::milliseconds resetTimeout = std::chrono::minutes(5));
    bool isCircuitOpen(const QString& component) const;
    void resetCircuit(const QString& component);

    // Health monitoring
    void startHealthCheck(const QString& component, 
                         std::function<bool()> healthCheck,
                         std::chrono::milliseconds interval = std::chrono::seconds(30));
    void stopHealthCheck(const QString& component);
    bool isComponentHealthy(const QString& component) const;

    // Configuration
    void setMaxErrorHistory(int maxErrors);
    void setErrorReportingEnabled(bool enabled);
    void setAutoRecoveryEnabled(bool enabled);

    // Statistics and monitoring
    QList<ErrorContext> getErrorHistory(const QString& component = QString()) const;
    QVariantMap getErrorStatistics(const QString& component = QString()) const;
    void clearErrorHistory(const QString& component = QString());

signals:
    void errorReported(const ErrorContext& context);
    void recoveryAttempted(const QString& component, const QString& operation, bool success);
    void circuitBreakerTripped(const QString& component);
    void circuitBreakerReset(const QString& component);
    void componentHealthChanged(const QString& component, bool healthy);
    void componentRestartRequested(const QString& component, const QString& action);
    void retryRequested(const QString& component, const QString& operation, const QString& message);
    void userPromptRequested(const QString& component, const QString& operation, 
                            const QString& description, const QString& errorMessage);

private slots:
    void performHealthCheck();

private:
    struct CircuitBreakerState {
        int failureCount = 0;
        int failureThreshold = 5;
        QDateTime lastFailure;
        std::chrono::milliseconds resetTimeout{300000}; // 5 minutes
        bool isOpen = false;
    };

    struct HealthCheckConfig {
        std::function<bool()> healthCheck;
        QTimer* timer;
        bool lastResult = true;
        std::chrono::milliseconds interval{30000};
    };

    RecoveryStrategy findRecoveryStrategy(const ErrorContext& context) const;
    bool executeRecoveryAction(RecoveryAction action, const ErrorContext& context, 
                              const RecoveryStrategy& strategy);
    bool performComponentRestart(const ErrorContext& context);
    bool performRetryOperation(const ErrorContext& context, const RecoveryStrategy& strategy);
    void updateCircuitBreaker(const QString& component, bool success);
    
    QMap<QString, QMap<QString, RecoveryStrategy>> componentStrategies_;
    QMap<ErrorSeverity, RecoveryStrategy> globalStrategies_;
    QMap<QString, CircuitBreakerState> circuitBreakers_;
    QMap<QString, HealthCheckConfig> healthChecks_;
    
    QList<ErrorContext> errorHistory_;
    int maxErrorHistory_ = 1000;
    bool errorReportingEnabled_ = true;
    bool autoRecoveryEnabled_ = true;
    
    // User prompt handling
    QMap<QString, ErrorContext> pendingUserPrompts_;
    
    std::unique_ptr<RetryManager> retryManager_;
};

// Convenience macros for error reporting
#define REPORT_ERROR(recovery, component, operation, message, severity) \
    (recovery)->reportError(component, operation, message, severity)

#define REPORT_INFO(recovery, component, operation, message) \
    (recovery)->reportError(component, operation, message, ErrorSeverity::Info)

#define REPORT_WARNING(recovery, component, operation, message) \
    (recovery)->reportError(component, operation, message, ErrorSeverity::Warning)

#define REPORT_ERROR_MSG(recovery, component, operation, message) \
    (recovery)->reportError(component, operation, message, ErrorSeverity::Error)

#define REPORT_CRITICAL(recovery, component, operation, message) \
    (recovery)->reportError(component, operation, message, ErrorSeverity::Critical)

#define REPORT_FATAL(recovery, component, operation, message) \
    (recovery)->reportError(component, operation, message, ErrorSeverity::Fatal)

// Recovery strategy builders
namespace RecoveryStrategies {
    inline RecoveryStrategy retryWithExponentialBackoff(int maxAttempts = 3) {
        RecoveryStrategy strategy;
        strategy.primaryAction = RecoveryAction::Retry;
        strategy.retryConfig = RetryConfigs::network();
        strategy.retryConfig.maxAttempts = maxAttempts;
        strategy.description = QString("Retry with exponential backoff (%1 attempts)").arg(maxAttempts);
        return strategy;
    }
    
    inline RecoveryStrategy fallbackWithRetry(std::function<bool()> fallbackFunc, int maxAttempts = 2) {
        RecoveryStrategy strategy;
        strategy.primaryAction = RecoveryAction::Retry;
        strategy.fallbackAction = RecoveryAction::Fallback;
        strategy.fallback = fallbackFunc;
        strategy.retryConfig = RetryConfigs::network();
        strategy.retryConfig.maxAttempts = maxAttempts;
        strategy.description = "Retry then fallback";
        return strategy;
    }
    
    inline RecoveryStrategy resetComponent(std::function<bool()> resetFunc) {
        RecoveryStrategy strategy;
        strategy.primaryAction = RecoveryAction::Reset;
        strategy.recovery = resetFunc;
        strategy.description = "Reset component state";
        return strategy;
    }
    
    inline RecoveryStrategy userPrompt(const QString& description) {
        RecoveryStrategy strategy;
        strategy.primaryAction = RecoveryAction::UserPrompt;
        strategy.description = description;
        return strategy;
    }
    
    inline RecoveryStrategy terminate(const QString& reason) {
        RecoveryStrategy strategy;
        strategy.primaryAction = RecoveryAction::Terminate;
        strategy.description = reason;
        return strategy;
    }
}

} // namespace Murmur

Q_DECLARE_METATYPE(Murmur::ErrorSeverity)
Q_DECLARE_METATYPE(Murmur::RecoveryAction)
Q_DECLARE_METATYPE(Murmur::ErrorContext)