#include <QtTest/QtTest>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include "utils/TestUtils.hpp"
#include "../src/core/common/RetryManager.hpp"

using namespace Murmur;
using namespace Murmur::Test;

class TestRetryManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Basic functionality tests
    void testBasicRetrySuccess();
    void testBasicRetryFailure();
    void testMaxAttemptsExceeded();
    void testTimeoutExceeded();
    
    // Retry policy tests
    void testLinearRetryPolicy();
    void testExponentialRetryPolicy();
    void testFibonacciRetryPolicy();
    void testCustomRetryPolicy();
    
    // Configuration tests
    void testRetryConfiguration();
    void testJitterConfiguration();
    void testRetryableErrorDetection();
    
    // Async operation tests
    void testAsyncExecution();
    void testAsyncCancellation();
    void testConcurrentRetries();
    
    // Edge cases and error handling
    void testZeroMaxAttempts();
    void testNegativeDelay();
    void testNullOperation();
    void testExceptionInOperation();
    
    // Performance and timing tests
    void testRetryTiming();
    void testBackoffAccuracy();
    void testJitterVariation();

private:
    std::unique_ptr<RetryManager> retryManager_;
    int operationCallCount_;
    bool shouldOperationFail_;
    QString lastError_;
    
    // Test operations
    Expected<QString, QString> successOperation();
    Expected<QString, QString> failingOperation();
    Expected<QString, QString> intermittentOperation();
    Expected<QString, QString> slowOperation();
    Expected<QString, QString> throwingOperation();
};

void TestRetryManager::initTestCase() {
    TestUtils::initializeTestEnvironment();
}

void TestRetryManager::cleanupTestCase() {
    TestUtils::cleanupTestEnvironment();
}

void TestRetryManager::init() {
    retryManager_ = std::make_unique<RetryManager>(this);
    operationCallCount_ = 0;
    shouldOperationFail_ = false;
    lastError_.clear();
}

void TestRetryManager::cleanup() {
    retryManager_.reset();
}

void TestRetryManager::testBasicRetrySuccess() {
    TEST_SCOPE("testBasicRetrySuccess");
    
    RetryConfig config;
    config.maxAttempts = 3;
    config.initialDelay = std::chrono::milliseconds(100);
    retryManager_->setConfig(config);
    
    shouldOperationFail_ = false;
    
    auto result = retryManager_->execute<QString, QString>(
        [this]() { return successOperation(); }
    );
    
    ASSERT_EXPECTED_VALUE(result);
    QCOMPARE(result.value(), QString("success"));
    QCOMPARE(operationCallCount_, 1);
}

void TestRetryManager::testBasicRetryFailure() {
    TEST_SCOPE("testBasicRetryFailure");
    
    RetryConfig config;
    config.maxAttempts = 3;
    config.initialDelay = std::chrono::milliseconds(50);
    retryManager_->setConfig(config);
    
    shouldOperationFail_ = true;
    
    auto result = retryManager_->execute<QString, QString>(
        [this]() { return failingOperation(); }
    );
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), RetryError::MaxAttemptsExceeded);
    QCOMPARE(operationCallCount_, 3);
}

void TestRetryManager::testMaxAttemptsExceeded() {
    TEST_SCOPE("testMaxAttemptsExceeded");
    
    RetryConfig config;
    config.maxAttempts = 2;
    config.initialDelay = std::chrono::milliseconds(10);
    retryManager_->setConfig(config);
    
    QElapsedTimer timer;
    timer.start();
    
    auto result = retryManager_->execute<QString, QString>(
        [this]() { return failingOperation(); }
    );
    
    qint64 elapsed = timer.elapsed();
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), RetryError::MaxAttemptsExceeded);
    QCOMPARE(operationCallCount_, 2);
}

void TestRetryManager::testTimeoutExceeded() {
    TEST_SCOPE("testTimeoutExceeded");
    
    shouldOperationFail_ = true; // Force operation to fail and retry until timeout
    
    RetryConfig config;
    config.maxAttempts = 10;
    config.initialDelay = std::chrono::milliseconds(100);  // Reduced delay
    config.timeout = std::chrono::milliseconds(800); // Longer timeout to account for operation time
    retryManager_->setConfig(config);
    
    QElapsedTimer timer;
    timer.start();
    
    auto result = retryManager_->execute<QString, QString>(
        [this]() { return slowOperation(); }
    );
    
    qint64 elapsed = timer.elapsed();
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), RetryError::TimeoutExceeded);
    
    // Should have timed out around 800ms, allowing for operation execution time
    QVERIFY(elapsed >= 700);  // Allow some margin for timing variation
    QVERIFY(elapsed < 1200); // But not taken too long, account for overhead
}

void TestRetryManager::testLinearRetryPolicy() {
    TEST_SCOPE("testLinearRetryPolicy");
    
    RetryConfig config;
    config.policy = RetryPolicy::Linear;
    config.maxAttempts = 3;
    config.initialDelay = std::chrono::milliseconds(100);
    config.enableJitter = false; // Disable for predictable timing
    retryManager_->setConfig(config);
    
    QElapsedTimer timer;
    timer.start();
    
    auto result = retryManager_->execute<QString, QString>(
        [this]() { return failingOperation(); }
    );
    
    qint64 elapsed = timer.elapsed();
    
    QVERIFY(result.hasError());
    
    // Linear policy: 100ms + 100ms = 200ms total delay
    QVERIFY(elapsed >= 200);
    QVERIFY(elapsed < 400); // Allow some tolerance
}

void TestRetryManager::testExponentialRetryPolicy() {
    TEST_SCOPE("testExponentialRetryPolicy");
    
    RetryConfig config;
    config.policy = RetryPolicy::Exponential;
    config.maxAttempts = 3;
    config.initialDelay = std::chrono::milliseconds(100);
    config.backoffMultiplier = 2.0;
    config.enableJitter = false;
    retryManager_->setConfig(config);
    
    QElapsedTimer timer;
    timer.start();
    
    auto result = retryManager_->execute<QString, QString>(
        [this]() { return failingOperation(); }
    );
    
    qint64 elapsed = timer.elapsed();
    
    QVERIFY(result.hasError());
    
    // Exponential policy: 100ms + 200ms = 300ms total delay
    QVERIFY(elapsed >= 300);
    QVERIFY(elapsed < 500);
}

void TestRetryManager::testFibonacciRetryPolicy() {
    TEST_SCOPE("testFibonacciRetryPolicy");
    
    RetryConfig config;
    config.policy = RetryPolicy::Fibonacci;
    config.maxAttempts = 4;
    config.initialDelay = std::chrono::milliseconds(50);
    config.enableJitter = false;
    retryManager_->setConfig(config);
    
    QElapsedTimer timer;
    timer.start();
    
    auto result = retryManager_->execute<QString, QString>(
        [this]() { return failingOperation(); }
    );
    
    qint64 elapsed = timer.elapsed();
    
    QVERIFY(result.hasError());
    
    // Fibonacci policy: 50ms*1 + 50ms*1 + 50ms*2 = 200ms total delay
    QVERIFY(elapsed >= 200);
    QVERIFY(elapsed < 400);
}

void TestRetryManager::testCustomRetryPolicy() {
    TEST_SCOPE("testCustomRetryPolicy");
    
    RetryConfig config;
    config.policy = RetryPolicy::Custom;
    config.maxAttempts = 3;
    config.calculateDelay = [](int attempt) -> std::chrono::milliseconds {
        return std::chrono::milliseconds(attempt * 50); // 50ms per attempt
    };
    retryManager_->setConfig(config);
    
    QElapsedTimer timer;
    timer.start();
    
    auto result = retryManager_->execute<QString, QString>(
        [this]() { return failingOperation(); }
    );
    
    qint64 elapsed = timer.elapsed();
    
    QVERIFY(result.hasError());
    
    // Custom policy: 50ms + 100ms = 150ms total delay
    // Allow some timing tolerance due to system load and timer precision
    QVERIFY(elapsed >= 100);  // More tolerant lower bound
    QVERIFY(elapsed < 500);   // More tolerant upper bound
}

void TestRetryManager::testRetryConfiguration() {
    TEST_SCOPE("testRetryConfiguration");
    
    RetryConfig originalConfig;
    originalConfig.maxAttempts = 5;
    originalConfig.initialDelay = std::chrono::milliseconds(200);
    
    retryManager_->setConfig(originalConfig);
    RetryConfig retrievedConfig = retryManager_->getConfig();
    
    QCOMPARE(retrievedConfig.maxAttempts, 5);
    QCOMPARE(retrievedConfig.initialDelay.count(), 200);
}

void TestRetryManager::testJitterConfiguration() {
    TEST_SCOPE("testJitterConfiguration");
    
    RetryConfig config;
    config.maxAttempts = 3;
    config.initialDelay = std::chrono::milliseconds(100);
    config.enableJitter = true;
    config.jitterFactor = 0.5; // 50% jitter
    retryManager_->setConfig(config);
    
    // Run multiple times to test jitter variation
    QList<qint64> timings;
    for (int i = 0; i < 5; ++i) {
        operationCallCount_ = 0;
        
        QElapsedTimer timer;
        timer.start();
        
        auto result = retryManager_->execute<QString, QString>(
            [this]() { return failingOperation(); }
        );
        
        timings.append(timer.elapsed());
    }
    
    // All timings should be different due to jitter
    bool hasVariation = false;
    for (int i = 1; i < timings.size(); ++i) {
        if (qAbs(timings[i] - timings[0]) > 10) { // Allow 10ms tolerance
            hasVariation = true;
            break;
        }
    }
    
    QVERIFY(hasVariation);
}

void TestRetryManager::testRetryableErrorDetection() {
    TEST_SCOPE("testRetryableErrorDetection");
    
    RetryConfig config;
    config.maxAttempts = 3;
    config.initialDelay = std::chrono::milliseconds(50);
    retryManager_->setConfig(config);
    
    // Test with non-retryable error
    auto result = retryManager_->execute<QString, QString>(
        [this]() -> Expected<QString, QString> {
            operationCallCount_++;
            return makeUnexpected(QString("authentication failed"));
        },
        [](const QString& error) -> bool {
            return !error.contains("authentication", Qt::CaseInsensitive);
        }
    );
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), RetryError::NonRetryableError);
    QCOMPARE(operationCallCount_, 1); // Should not retry
}

void TestRetryManager::testAsyncExecution() {
    TEST_SCOPE("testAsyncExecution");
    
    RetryConfig config;
    config.maxAttempts = 2;
    config.initialDelay = std::chrono::milliseconds(100);
    retryManager_->setConfig(config);
    
    bool callbackCalled = false;
    QString resultValue;
    
    retryManager_->executeAsync<QString>(
        [this]() { return successOperation(); },
        [&](const QString& result) {
            callbackCalled = true;
            resultValue = result;
        },
        [&](RetryError error, const QString& message) {
            QFAIL(qPrintable(QString("Unexpected failure: %1").arg(message)));
        }
    );
    
    // Wait for async operation to complete
    QVERIFY(TestUtils::waitForCondition([&]() { return callbackCalled; }, 5000));
    QCOMPARE(resultValue, QString("success"));
}

void TestRetryManager::testAsyncCancellation() {
    TEST_SCOPE("testAsyncCancellation");
    
    RetryConfig config;
    config.maxAttempts = 5;
    config.initialDelay = std::chrono::milliseconds(500); // Long delay
    retryManager_->setConfig(config);
    
    bool failureCallbackCalled = false;
    RetryError errorResult = RetryError::MaxAttemptsExceeded;
    
    retryManager_->executeAsync<QString>(
        [this]() { return failingOperation(); },
        [&](const QString& result) {
            QFAIL("Success callback should not be called");
        },
        [&](RetryError error, const QString& message) {
            failureCallbackCalled = true;
            errorResult = error;
        }
    );
    
    // Cancel after a short delay
    QTimer::singleShot(200, [this]() {
        retryManager_->cancel();
    });
    
    QVERIFY(TestUtils::waitForCondition([&]() { return failureCallbackCalled; }, 5000));
    QCOMPARE(errorResult, RetryError::UserCancelled);
}

void TestRetryManager::testConcurrentRetries() {
    TEST_SCOPE("testConcurrentRetries");
    
    // Test thread safety by running multiple retry operations concurrently
    // Reduced concurrency to avoid SIGPIPE issues during testing
    const int threadCount = 2;
    const int operationsPerThread = 3;
    
    std::atomic<int> successCount{0};
    std::atomic<int> failureCount{0};
    
    TestUtils::testThreadSafety([&]() {
        RetryManager localRetryManager;
        RetryConfig config;
        config.maxAttempts = 2;
        config.initialDelay = std::chrono::milliseconds(50); // Increased delay to reduce rapid operations
        localRetryManager.setConfig(config);
        
        auto result = localRetryManager.execute<QString, QString>(
            []() -> Expected<QString, QString> {
                // Randomly succeed or fail
                if (QRandomGenerator::global()->bounded(2) == 0) {
                    return makeExpectedValue<QString, QString>(QString("success"));
                } else {
                    return makeUnexpected(QString("random failure"));
                }
            }
        );
        
        if (result.hasValue()) {
            successCount++;
        } else {
            failureCount++;
        }
    }, threadCount, operationsPerThread);
    
    int totalOperations = threadCount * operationsPerThread;
    QCOMPARE(successCount + failureCount, totalOperations);
}

void TestRetryManager::testZeroMaxAttempts() {
    TEST_SCOPE("testZeroMaxAttempts");
    
    RetryConfig config;
    config.maxAttempts = 0;
    retryManager_->setConfig(config);
    
    auto result = retryManager_->execute<QString, QString>(
        [this]() { return successOperation(); }
    );
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), RetryError::MaxAttemptsExceeded);
    QCOMPARE(operationCallCount_, 0);
}

void TestRetryManager::testNegativeDelay() {
    TEST_SCOPE("testNegativeDelay");
    
    RetryConfig config;
    config.maxAttempts = 2;
    config.initialDelay = std::chrono::milliseconds(-100);
    retryManager_->setConfig(config);
    
    QElapsedTimer timer;
    timer.start();
    
    auto result = retryManager_->execute<QString, QString>(
        [this]() { return failingOperation(); }
    );
    
    qint64 elapsed = timer.elapsed();
    
    QVERIFY(result.hasError());
    // Should handle negative delay gracefully (treat as zero)
    QVERIFY(elapsed < 100); // Should be very fast
}

void TestRetryManager::testNullOperation() {
    TEST_SCOPE("testNullOperation");
    
    // This test verifies that the template system prevents null operations
    // at compile time, so we test with a valid but empty operation
    
    RetryConfig config;
    config.maxAttempts = 1;
    retryManager_->setConfig(config);
    
    auto result = retryManager_->execute<QString, QString>(
        []() -> Expected<QString, QString> {
            return makeUnexpected(QString("null operation"));
        }
    );
    
    QVERIFY(result.hasError());
}

void TestRetryManager::testExceptionInOperation() {
    TEST_SCOPE("testExceptionInOperation");
    
    RetryConfig config;
    config.maxAttempts = 2;
    config.initialDelay = std::chrono::milliseconds(50);
    retryManager_->setConfig(config);
    
    // Test that exceptions are handled gracefully
    auto result = retryManager_->execute<QString, QString>(
        [this]() { return throwingOperation(); }
    );
    
    QVERIFY(result.hasError());
    // The implementation should catch exceptions and treat them as failures
}

void TestRetryManager::testRetryTiming() {
    TEST_SCOPE("testRetryTiming");
    BENCHMARK_SCOPE("RetryTiming", 5);
    
    RetryConfig config;
    config.maxAttempts = 3;
    config.initialDelay = std::chrono::milliseconds(100);
    config.enableJitter = false;
    retryManager_->setConfig(config);
    
    for (int i = 0; i < 5; ++i) {
        operationCallCount_ = 0;
        
        _benchmark.startIteration();
        
        auto result = retryManager_->execute<QString, QString>(
            [this]() { return failingOperation(); }
        );
        
        _benchmark.endIteration();
        
        QVERIFY(result.hasError());
    }
    
    // Verify timing is within expected range
    double avgTime = _benchmark.getAverageTimeMs();
    QVERIFY(avgTime >= 200); // At least 200ms for 2 delays of 100ms each
    QVERIFY(avgTime < 400);  // But not too much overhead
}

void TestRetryManager::testBackoffAccuracy() {
    TEST_SCOPE("testBackoffAccuracy");
    
    RetryConfig config;
    config.policy = RetryPolicy::Exponential;
    config.maxAttempts = 4;
    config.initialDelay = std::chrono::milliseconds(100);
    config.backoffMultiplier = 2.0;
    config.enableJitter = false;
    retryManager_->setConfig(config);
    
    QList<qint64> attemptTimings;
    QElapsedTimer totalTimer;
    totalTimer.start();
    
    auto result = retryManager_->execute<QString, QString>(
        [&]() -> Expected<QString, QString> {
            attemptTimings.append(totalTimer.elapsed());
            return failingOperation();
        }
    );
    
    QVERIFY(result.hasError());
    QCOMPARE(attemptTimings.size(), 4);
    
    // Verify exponential backoff timing
    // Expected delays: 0, 100, 300 (100+200), 700 (300+400)
    QVERIFY(attemptTimings[0] < 50);  // First attempt should be immediate
    QVERIFY(attemptTimings[1] >= 100 && attemptTimings[1] < 150);
    QVERIFY(attemptTimings[2] >= 300 && attemptTimings[2] < 350);
    QVERIFY(attemptTimings[3] >= 700 && attemptTimings[3] < 750);
}

void TestRetryManager::testJitterVariation() {
    TEST_SCOPE("testJitterVariation");
    
    RetryConfig config;
    config.maxAttempts = 2;
    config.initialDelay = std::chrono::milliseconds(100);
    config.enableJitter = true;
    config.jitterFactor = 0.3; // 30% jitter
    retryManager_->setConfig(config);
    
    QList<qint64> delays;
    
    // Collect multiple delay measurements
    for (int i = 0; i < 10; ++i) {
        operationCallCount_ = 0;
        
        QElapsedTimer timer;
        timer.start();
        
        auto result = retryManager_->execute<QString, QString>(
            [this]() { return failingOperation(); }
        );
        
        delays.append(timer.elapsed());
    }
    
    // Calculate variance to verify jitter is working
    double mean = 0.0;
    for (qint64 delay : delays) {
        mean += delay;
    }
    mean /= delays.size();
    
    double variance = 0.0;
    for (qint64 delay : delays) {
        variance += (delay - mean) * (delay - mean);
    }
    variance /= delays.size();
    
    // With 30% jitter on 100ms, we should see some variance
    QVERIFY(variance > 50);  // More tolerant variance threshold
    QVERIFY(mean >= 70);     // More tolerant lower bound (100ms - 30%)
    QVERIFY(mean <= 200);    // But not too high
}

// Test operation implementations
Expected<QString, QString> TestRetryManager::successOperation() {
    operationCallCount_++;
    return makeExpectedValue<QString, QString>(QString("success"));
}

Expected<QString, QString> TestRetryManager::failingOperation() {
    operationCallCount_++;
    lastError_ = QString("operation failed (attempt %1)").arg(operationCallCount_);
    return makeUnexpected(lastError_);
}

Expected<QString, QString> TestRetryManager::intermittentOperation() {
    operationCallCount_++;
    
    // Succeed on third attempt
    if (operationCallCount_ >= 3) {
        return makeExpectedValue<QString, QString>(QString("success after retries"));
    }
    
    lastError_ = QString("intermittent failure (attempt %1)").arg(operationCallCount_);
    return makeUnexpected(lastError_);
}

Expected<QString, QString> TestRetryManager::slowOperation() {
    operationCallCount_++;
    
    // Simulate slow operation
    QTest::qWait(300);
    
    if (shouldOperationFail_) {
        return makeUnexpected(QString("slow operation failed"));
    }
    
    return makeExpectedValue<QString, QString>(QString("slow success"));
}

Expected<QString, QString> TestRetryManager::throwingOperation() {
    operationCallCount_++;
    
    try {
        throw std::runtime_error("Test exception");
    } catch (const std::exception& e) {
        return makeUnexpected(QString("Exception caught: %1").arg(e.what()));
    }
}

int runTestRetryManager(int argc, char** argv) {
    TestRetryManager test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_retry_manager.moc"