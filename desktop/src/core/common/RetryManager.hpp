#pragma once

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QThread>
#include <QtCore/QVariant>
#include <functional>
#include <chrono>
#include "Expected.hpp"

namespace Murmur {

enum class RetryPolicy {
    None,           // No retries
    Linear,         // Fixed delay between retries
    Exponential,    // Exponentially increasing delay
    Fibonacci,      // Fibonacci sequence delays
    Custom          // User-defined retry logic
};

enum class RetryError {
    MaxAttemptsExceeded,
    TimeoutExceeded,
    NonRetryableError,
    UserCancelled
};

struct RetryConfig {
    RetryPolicy policy = RetryPolicy::Exponential;
    int maxAttempts = 3;
    std::chrono::milliseconds initialDelay{1000};
    std::chrono::milliseconds maxDelay{30000};
    std::chrono::milliseconds timeout{0}; // 0 = no timeout
    double backoffMultiplier = 2.0;
    double jitterFactor = 0.1; // 10% jitter
    bool enableJitter = true;
    
    // Custom retry condition
    std::function<bool(int attempt, const QString& error)> shouldRetry = nullptr;
    
    // Custom delay calculation
    std::function<std::chrono::milliseconds(int attempt)> calculateDelay = nullptr;
};

/**
 * @brief Comprehensive retry manager with configurable policies
 * 
 * Provides robust retry mechanisms for network operations, file I/O,
 * hardware initialization, and other potentially failing operations.
 */
class RetryManager : public QObject {
    Q_OBJECT

public:
    explicit RetryManager(QObject* parent = nullptr);
    explicit RetryManager(const RetryConfig& config, QObject* parent = nullptr);

    // Template function for executing operations with retry
    template<typename T, typename ErrorType>
    Expected<T, RetryError> execute(
        std::function<Expected<T, ErrorType>()> operation,
        std::function<bool(const ErrorType&)> isRetryable = nullptr
    );

    // Async version with Qt signals
    template<typename T>
    void executeAsync(
        std::function<Expected<T, QString>()> operation,
        std::function<void(const T&)> onSuccess,
        std::function<void(RetryError, const QString&)> onFailure,
        std::function<bool(const QString&)> isRetryable = nullptr
    );

    // Configuration
    void setConfig(const RetryConfig& config);
    RetryConfig getConfig() const;
    
    // Control
    void cancel();
    bool isCancelled() const;
    bool isRunning() const;
    
    // Statistics
    int getCurrentAttempt() const;
    std::chrono::milliseconds getElapsedTime() const;
    std::chrono::milliseconds getNextDelay() const;

signals:
    void attemptStarted(int attempt);
    void attemptFailed(int attempt, const QString& error);
    void retryScheduled(int nextAttempt, int delayMs);
    void operationCompleted(bool success);
    void operationCancelled();

private slots:
    void executeNextAttempt();

private:
    std::chrono::milliseconds calculateDelayForAttempt(int attempt) const;
    bool shouldRetryError(const QString& error) const;
    void scheduleNextAttempt();
    void reset();

    RetryConfig config_;
    QTimer* retryTimer_;
    QElapsedTimer elapsedTimer_;
    
    int currentAttempt_ = 0;
    bool cancelled_ = false;
    bool running_ = false;
    
    // Current operation context
    std::function<Expected<QVariant, QString>()> currentOperation_;
    std::function<void(const QVariant&)> currentSuccessCallback_;
    std::function<void(RetryError, const QString&)> currentFailureCallback_;
    std::function<bool(const QString&)> currentRetryableCheck_;
};

// Template implementations
template<typename T, typename ErrorType>
Expected<T, RetryError> RetryManager::execute(
    std::function<Expected<T, ErrorType>()> operation,
    std::function<bool(const ErrorType&)> isRetryable
) {
    reset();
    elapsedTimer_.start();
    
    for (currentAttempt_ = 1; currentAttempt_ <= config_.maxAttempts; ++currentAttempt_) {
        if (cancelled_) {
            return makeUnexpected(RetryError::UserCancelled);
        }
        
        // Check timeout
        if (config_.timeout.count() > 0 && 
            elapsedTimer_.elapsed() > config_.timeout.count()) {
            return makeUnexpected(RetryError::TimeoutExceeded);
        }
        
        emit attemptStarted(currentAttempt_);
        
        auto result = operation();
        if (result.hasValue()) {
            emit operationCompleted(true);
            return result.value();
        }
        
        // Check if error is retryable
        bool shouldRetry = true;
        if (isRetryable) {
            shouldRetry = isRetryable(result.error());
        }
        
        if (!shouldRetry) {
            emit operationCompleted(false);
            return makeUnexpected(RetryError::NonRetryableError);
        }
        
        QString errorStr = QString("Attempt %1 failed").arg(currentAttempt_);
        emit attemptFailed(currentAttempt_, errorStr);
        
        // Don't delay after the last attempt
        if (currentAttempt_ < config_.maxAttempts) {
            auto delay = calculateDelayForAttempt(currentAttempt_);
            emit retryScheduled(currentAttempt_ + 1, delay.count());
            
            // Synchronous delay
            QThread::msleep(delay.count());
        }
    }
    
    emit operationCompleted(false);
    return makeUnexpected(RetryError::MaxAttemptsExceeded);
}

template<typename T>
void RetryManager::executeAsync(
    std::function<Expected<T, QString>()> operation,
    std::function<void(const T&)> onSuccess,
    std::function<void(RetryError, const QString&)> onFailure,
    std::function<bool(const QString&)> isRetryable
) {
    // Store callbacks with type erasure
    currentOperation_ = [operation]() -> Expected<QVariant, QString> {
        auto result = operation();
        if (result.hasValue()) {
            return QVariant::fromValue(result.value());
        }
        return makeUnexpected(result.error());
    };
    
    currentSuccessCallback_ = [onSuccess](const QVariant& value) {
        onSuccess(value.value<T>());
    };
    
    currentFailureCallback_ = onFailure;
    currentRetryableCheck_ = isRetryable;
    
    reset();
    running_ = true;
    elapsedTimer_.start();
    
    executeNextAttempt();
}

// Network-specific retry configurations
namespace RetryConfigs {
    inline RetryConfig network() {
        RetryConfig config;
        config.policy = RetryPolicy::Exponential;
        config.maxAttempts = 5;
        config.initialDelay = std::chrono::milliseconds(1000);
        config.maxDelay = std::chrono::milliseconds(30000);
        config.timeout = std::chrono::milliseconds(300000); // 5 minutes
        config.backoffMultiplier = 2.0;
        config.enableJitter = true;
        return config;
    }
    
    inline RetryConfig fileIO() {
        RetryConfig config;
        config.policy = RetryPolicy::Linear;
        config.maxAttempts = 3;
        config.initialDelay = std::chrono::milliseconds(500);
        config.maxDelay = std::chrono::milliseconds(2000);
        config.timeout = std::chrono::milliseconds(30000);
        config.enableJitter = false;
        return config;
    }
    
    inline RetryConfig hardware() {
        RetryConfig config;
        config.policy = RetryPolicy::Fibonacci;
        config.maxAttempts = 4;
        config.initialDelay = std::chrono::milliseconds(250);
        config.maxDelay = std::chrono::milliseconds(5000);
        config.timeout = std::chrono::milliseconds(60000);
        config.enableJitter = true;
        return config;
    }
    
    inline RetryConfig database() {
        RetryConfig config;
        config.policy = RetryPolicy::Exponential;
        config.maxAttempts = 3;
        config.initialDelay = std::chrono::milliseconds(100);
        config.maxDelay = std::chrono::milliseconds(1000);
        config.timeout = std::chrono::milliseconds(10000);
        config.backoffMultiplier = 1.5;
        config.enableJitter = false;
        return config;
    }
}

} // namespace Murmur