#pragma once

#include <QtTest/QtTest>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QEventLoop>
#include <QtTest/QSignalSpy>
#include <QtConcurrent/QtConcurrent>
#include <memory>
#include <type_traits>

#include "../../src/core/common/Expected.hpp"
#include "../../src/core/common/Logger.hpp"
#include "../../src/core/media/FFmpegWrapper.hpp"
#include "../../src/core/security/InfoHashValidator.hpp"

namespace Murmur {
namespace Test {

/**
 * @brief Test utilities for the Murmur desktop client
 * 
 * Provides common functionality for unit tests, integration tests,
 * and performance benchmarks.
 */
class TestUtils : public QObject {
    Q_OBJECT

public:
    explicit TestUtils(QObject* parent = nullptr);
    ~TestUtils();

    // Test environment setup
    static void initializeTestEnvironment();
    static void cleanupTestEnvironment();
    
    // Temporary directory management
    static QString createTempDirectory(const QString& prefix = "murmur_test");
    static void cleanupTempDirectory(const QString& path);
    static QString getTempPath();
    
    // Test file creation
    static QString createTestVideoFile(const QString& directory, int durationSeconds = 10, const QString& format = "mp4");
    static QString createTestAudioFile(const QString& directory, int durationSeconds = 5, const QString& format = "wav");
    static QString createTestTextFile(const QString& directory, const QString& content, const QString& filename = "test.txt");
    static QByteArray createTestImageData(int width = 100, int height = 100);
    
    // Test torrent creation
    static QString createTestTorrentFile(const QString& directory, const QStringList& fileNames);
    static QString createTestMagnetLink(const QString& name = "Test Torrent");
    
    // Database utilities
    static QString createTestDatabase(const QString& directory = QString());
    static void populateTestDatabase(const QString& dbPath);
    static bool verifyDatabaseIntegrity(const QString& dbPath);
    
    // Async testing utilities
    template<typename T>
    static T waitForFuture(QFuture<T> future, int timeoutMs = 5000);
    
    static bool waitForSignal(QObject* sender, const char* signal, int timeoutMs = 5000);
    static bool waitForCondition(std::function<bool()> condition, int timeoutMs = 5000, int checkIntervalMs = 100);
    
    // Performance measurement
    static qint64 measureExecutionTime(std::function<void()> operation);
    static QPair<qint64, double> measureMemoryUsage(std::function<void()> operation);
    
    // Mock data generation
    static QStringList generateRandomStrings(int count, int minLength = 5, int maxLength = 20);
    static QByteArray generateRandomData(int size);
    static QJsonObject generateTestTorrentMetadata();
    static QJsonObject generateTestMediaMetadata();
    
    // Network testing utilities
    static bool isNetworkAvailable();
    static QString startTestHttpServer(int port = 0); // Returns actual URL
    static void stopTestHttpServer();
    
    // Dependency checking
    static bool isFFmpegAvailable();
    static bool isWhisperAvailable();
    static bool isSQLiteAvailable();
    static bool isTestVideoAvailable();
    
    // Logging utilities
    static void enableTestLogging();
    static void disableTestLogging();
    static QStringList getTestLogs();
    static void clearTestLogs();
    
    // Error simulation
    static void simulateNetworkError();
    static void simulateDiskFullError();
    static void simulateMemoryPressure();
    static void clearSimulatedErrors();
    
    // Validation utilities
    static bool compareFiles(const QString& file1, const QString& file2);
    static bool validateVideoFile(const QString& filePath);
    static bool validateAudioFile(const QString& filePath);
    static bool validateDatabaseFile(const QString& filePath);
    
    // Test assertions with better error messages
    template<typename T, typename E>
    static void assertExpectedValue(const Expected<T, E>& result, const QString& context = QString());
    
    template<typename T, typename E>
    static void assertExpectedError(const Expected<T, E>& result, E expectedError, const QString& context = QString());
    
    static void assertFileExists(const QString& filePath, const QString& context = QString());
    static void assertDirectoryExists(const QString& dirPath, const QString& context = QString());
    static void assertFileNotExists(const QString& filePath, const QString& context = QString());
    
    // Thread safety testing
    static void testThreadSafety(std::function<void()> operation, int threadCount = 10, int iterationsPerThread = 100);
    
    // Resource monitoring
    static void startResourceMonitoring();
    static void stopResourceMonitoring();
    static QJsonObject getResourceUsageReport();
    
    // Public logging for test utilities
    static void logMessage(const QString& message);
    
    // Real media file helpers
    static QString getRealSampleVideoFile();
    static QString getRealSampleAudioFile();
    static bool validateRealMediaFile(const QString& filePath);
    
    // Test asset helpers using QFINDTESTDATA
    static QString getTestAssetPath(const QString& filename);
    static QString getTestVideoAsset();
    static QString getTestAudioToneAsset();
    static QString getTestAudioSpeechAsset();
    static bool hasFFmpegFallback();

private:
    static QTemporaryDir* tempDir_;
    static QStringList testLogs_;
    static QTimer* resourceMonitorTimer_;
    static QJsonObject resourceBaseline_;
    
    static void logTestMessage(const QString& message);
    static void monitorResources();
};

/**
 * @brief RAII helper for test scope management
 */
class TestScope {
public:
    explicit TestScope(const QString& testName);
    ~TestScope();
    
    QString getTempDirectory() const;
    void addCleanupCallback(std::function<void()> callback);

private:
    QString testName_;
    QString tempDirectory_;
    QStringList createdFiles_;
    std::vector<std::function<void()>> cleanupCallbacks_;
};

/**
 * @brief Performance benchmark helper
 */
class BenchmarkScope {
public:
    explicit BenchmarkScope(const QString& operationName, int iterations = 1);
    ~BenchmarkScope();
    
    void startIteration();
    void endIteration();
    
    double getAverageTimeMs() const;
    double getMinTimeMs() const;
    double getMaxTimeMs() const;
    double getStandardDeviation() const;

private:
    QString operationName_;
    int totalIterations_;
    int currentIteration_;
    QElapsedTimer timer_;
    QList<qint64> measurements_;
    qint64 iterationStart_;
};

// Template implementations
template<typename T>
T TestUtils::waitForFuture(QFuture<T> future, int timeoutMs) {
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    
    // Connect future watcher
    QFutureWatcher<T> watcher;
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    
    watcher.setFuture(future);
    timer.start(timeoutMs);
    
    loop.exec();
    
    if (!future.isFinished()) {
        logMessage(QString("waitForFuture timeout after %1ms").arg(timeoutMs));
        
        // For Expected types, return a timeout error
        if constexpr (std::is_same_v<T, Expected<QString, FFmpegError>>) {
            return makeUnexpected(FFmpegError::TimeoutError);
        } else if constexpr (std::is_same_v<T, Expected<MediaFileInfo, FFmpegError>>) {
            return makeUnexpected(FFmpegError::TimeoutError);
        } else {
            // For other types, return default-constructed value
            static_assert(std::is_default_constructible_v<T>, "T must be default constructible for timeout case");
            return T{};
        }
    }
    
    return future.result();
}

template<typename T, typename E>
void TestUtils::assertExpectedValue(const Expected<T, E>& result, const QString& context) {
    if (result.hasError()) {
        QString message = QString("Expected value but got error");
        if (!context.isEmpty()) {
            message += QString(" in %1").arg(context);
        }
        message += QString(": error code %1").arg(static_cast<int>(result.error()));
        QFAIL(qPrintable(message));
    }
}

template<typename T, typename E>
void TestUtils::assertExpectedError(const Expected<T, E>& result, E expectedError, const QString& context) {
    if (result.hasValue()) {
        QString message = QString("Expected error but got value");
        if (!context.isEmpty()) {
            message += QString(" in %1").arg(context);
        }
        QFAIL(qPrintable(message));
    }
    
    if (result.error() != expectedError) {
        QString message = QString("Expected error %1 but got error %2")
                         .arg(static_cast<int>(expectedError))
                         .arg(static_cast<int>(result.error()));
        if (!context.isEmpty()) {
            message += QString(" in %1").arg(context);
        }
        QFAIL(qPrintable(message));
    }
}

// Convenience macros for testing
#define ASSERT_EXPECTED_VALUE(result) TestUtils::assertExpectedValue(result, QString("%1:%2").arg(__FILE__).arg(__LINE__))
#define ASSERT_EXPECTED_ERROR(result, error) TestUtils::assertExpectedError(result, error, QString("%1:%2").arg(__FILE__).arg(__LINE__))
#define ASSERT_FILE_EXISTS(path) TestUtils::assertFileExists(path, QString("%1:%2").arg(__FILE__).arg(__LINE__))
#define ASSERT_FILE_NOT_EXISTS(path) TestUtils::assertFileNotExists(path, QString("%1:%2").arg(__FILE__).arg(__LINE__))

#define TEST_SCOPE(name) TestScope _testScope(name)
#define BENCHMARK_SCOPE(name, iterations) BenchmarkScope _benchmark(name, iterations)

} // namespace Test
} // namespace Murmur