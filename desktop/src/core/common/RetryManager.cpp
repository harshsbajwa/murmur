#include "RetryManager.hpp"
#include "Logger.hpp"
#include <QtCore/QRandomGenerator>
#include <QtCore/QThread>
#include <algorithm>

namespace Murmur {

RetryManager::RetryManager(QObject* parent)
    : QObject(parent)
    , retryTimer_(new QTimer(this)) {
    
    retryTimer_->setSingleShot(true);
    connect(retryTimer_, &QTimer::timeout, this, &RetryManager::executeNextAttempt);
}

RetryManager::RetryManager(const RetryConfig& config, QObject* parent)
    : RetryManager(parent) {
    config_ = config;
}

void RetryManager::setConfig(const RetryConfig& config) {
    config_ = config;
}

RetryConfig RetryManager::getConfig() const {
    return config_;
}

void RetryManager::cancel() {
    cancelled_ = true;
    running_ = false;
    retryTimer_->stop();
    
    // Call failure callback for async operations
    if (currentFailureCallback_) {
        currentFailureCallback_(RetryError::UserCancelled, "Operation cancelled by user");
    }
    
    emit operationCancelled();
}

bool RetryManager::isCancelled() const {
    return cancelled_;
}

bool RetryManager::isRunning() const {
    return running_;
}

int RetryManager::getCurrentAttempt() const {
    return currentAttempt_;
}

std::chrono::milliseconds RetryManager::getElapsedTime() const {
    if (elapsedTimer_.isValid()) {
        return std::chrono::milliseconds(elapsedTimer_.elapsed());
    }
    return std::chrono::milliseconds(0);
}

std::chrono::milliseconds RetryManager::getNextDelay() const {
    if (currentAttempt_ < config_.maxAttempts) {
        return calculateDelayForAttempt(currentAttempt_);
    }
    return std::chrono::milliseconds(0);
}

void RetryManager::executeNextAttempt() {
    if (cancelled_ || !running_) {
        return;
    }
    
    ++currentAttempt_;
    
    // Check timeout
    if (config_.timeout.count() > 0 && 
        elapsedTimer_.elapsed() > config_.timeout.count()) {
        running_ = false;
        if (currentFailureCallback_) {
            currentFailureCallback_(RetryError::TimeoutExceeded, "Operation timed out");
        }
        emit operationCompleted(false);
        return;
    }
    
    // Check max attempts
    if (currentAttempt_ > config_.maxAttempts) {
        running_ = false;
        if (currentFailureCallback_) {
            currentFailureCallback_(RetryError::MaxAttemptsExceeded, "Maximum retry attempts exceeded");
        }
        emit operationCompleted(false);
        return;
    }
    
    emit attemptStarted(currentAttempt_);
    Logger::instance().info("Executing attempt {}/{}", currentAttempt_, config_.maxAttempts);
    
    auto result = currentOperation_();
    if (result.hasValue()) {
        running_ = false;
        if (currentSuccessCallback_) {
            currentSuccessCallback_(result.value());
        }
        emit operationCompleted(true);
        Logger::instance().info("Operation succeeded on attempt {}", currentAttempt_);
        return;
    }
    
    QString error = result.error();
    emit attemptFailed(currentAttempt_, error);
    Logger::instance().warn("Attempt {} failed: {}", currentAttempt_, error.toStdString());
    
    // Check if error is retryable
    if (!shouldRetryError(error)) {
        running_ = false;
        if (currentFailureCallback_) {
            currentFailureCallback_(RetryError::NonRetryableError, error);
        }
        emit operationCompleted(false);
        Logger::instance().error("Non-retryable error encountered: {}", error.toStdString());
        return;
    }
    
    // Schedule next attempt if we haven't exceeded limits
    if (currentAttempt_ < config_.maxAttempts) {
        scheduleNextAttempt();
    } else {
        running_ = false;
        if (currentFailureCallback_) {
            currentFailureCallback_(RetryError::MaxAttemptsExceeded, "Maximum retry attempts exceeded");
        }
        emit operationCompleted(false);
    }
}

std::chrono::milliseconds RetryManager::calculateDelayForAttempt(int attempt) const {
    std::chrono::milliseconds delay;
    
    if (config_.calculateDelay) {
        delay = config_.calculateDelay(attempt);
    } else {
        switch (config_.policy) {
            case RetryPolicy::None:
                delay = std::chrono::milliseconds(0);
                break;
                
            case RetryPolicy::Linear:
                delay = config_.initialDelay;
                break;
                
            case RetryPolicy::Exponential: {
                double multiplier = std::pow(config_.backoffMultiplier, attempt - 1);
                delay = std::chrono::milliseconds(
                    static_cast<long long>(config_.initialDelay.count() * multiplier)
                );
                break;
            }
            
            case RetryPolicy::Fibonacci: {
                // Calculate Fibonacci number for delay
                std::vector<int> fib = {1, 1};
                for (int i = 2; i <= attempt; ++i) {
                    fib.push_back(fib[i-1] + fib[i-2]);
                }
                delay = std::chrono::milliseconds(
                    config_.initialDelay.count() * fib[std::min(attempt, static_cast<int>(fib.size() - 1))]
                );
                break;
            }
            
            case RetryPolicy::Custom:
                delay = config_.initialDelay;
                break;
        }
    }
    
    // Apply jitter if enabled
    if (config_.enableJitter && config_.jitterFactor > 0.0) {
        double jitterRange = delay.count() * config_.jitterFactor;
        double jitter = (QRandomGenerator::global()->generateDouble() - 0.5) * 2.0 * jitterRange;
        delay = std::chrono::milliseconds(
            static_cast<long long>(std::max(0.0, delay.count() + jitter))
        );
    }
    
    // Enforce maximum delay
    if (delay > config_.maxDelay) {
        delay = config_.maxDelay;
    }
    
    return delay;
}

bool RetryManager::shouldRetryError(const QString& error) const {
    if (config_.shouldRetry) {
        return config_.shouldRetry(currentAttempt_, error);
    }
    
    // Default retry logic - retry most errors except for specific non-retryable ones
    QStringList nonRetryableErrors = {
        "authentication failed",
        "unauthorized",
        "forbidden",
        "not found",
        "method not allowed",
        "invalid request",
        "malformed",
        "syntax error",
        "parse error",
        "invalid format",
        "unsupported",
        "cancelled",
        "aborted"
    };
    
    QString lowerError = error.toLower();
    for (const QString& nonRetryable : nonRetryableErrors) {
        if (lowerError.contains(nonRetryable)) {
            return false;
        }
    }
    
    return true;
}

void RetryManager::scheduleNextAttempt() {
    auto delay = calculateDelayForAttempt(currentAttempt_);
    emit retryScheduled(currentAttempt_ + 1, delay.count());
    
    Logger::instance().info("Scheduling retry attempt {} in {}ms", 
                           currentAttempt_ + 1, delay.count());
    
    retryTimer_->start(delay.count());
}

void RetryManager::reset() {
    currentAttempt_ = 0;
    cancelled_ = false;
    running_ = false;
    retryTimer_->stop();
    elapsedTimer_.invalidate();
}

} // namespace Murmur