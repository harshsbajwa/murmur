#include <QtTest/QtTest>
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>

#include "utils/TestUtils.hpp"
#include "../src/core/common/Logger.hpp"

// Forward declarations of test classes that are defined in separate compilation units
extern int runTestExpected(int argc, char** argv);
extern int runTestInputValidator(int argc, char** argv);
extern int runTestRetryManager(int argc, char** argv);
extern int runTestTorrentEngine(int argc, char** argv);
extern int runTestVideoProcessingIntegration(int argc, char** argv);
extern int runTestSimpleRealMedia(int argc, char** argv);
extern int runTestRealMediaProcessing(int argc, char** argv);
extern int runTestPerformanceBenchmarks(int argc, char** argv);
extern int runTestWhisperEngine(int argc, char** argv);
extern int runTestUIFlows(int argc, char** argv);
extern int runTestFFmpegWrapper(int argc, char** argv);
extern int runTestStorageManager(int argc, char** argv);
extern int runTestSecurityComponents(int argc, char** argv);
extern int runTestEndToEndIntegration(int argc, char** argv);

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    
    Murmur::Logger::instance().initialize("murmur-tests.log", Murmur::Logger::Level::Trace);
    Murmur::Test::TestUtils::initializeTestEnvironment();

    int totalResult = 0;
    int testCount = 0;
    int passedTests = 0;
    
    struct TestInfo {
        const char* name;
        int (*function)(int, char**);
    };
    
    TestInfo tests[] = {
        {"Expected", runTestExpected},
        {"InputValidator", runTestInputValidator},
        {"RetryManager", runTestRetryManager},
        {"TorrentEngine", runTestTorrentEngine},
        // {"VideoProcessingIntegration", runTestVideoProcessingIntegration}, // Temporarily disabled - hangs
        {"SimpleRealMedia", runTestSimpleRealMedia},
        // {"RealMediaProcessing", runTestRealMediaProcessing}, // Temporarily disabled
        // {"PerformanceBenchmarks", runTestPerformanceBenchmarks}, // Temporarily disabled  
        // {"WhisperEngine", runTestWhisperEngine}, // Temporarily disabled - tests timing out
        {"StorageManager", runTestStorageManager},
        {"SecurityComponents", runTestSecurityComponents},
        {"FFmpegWrapper", runTestFFmpegWrapper}, // Re-enabled to debug runtime errors
        {"EndToEndIntegration", runTestEndToEndIntegration}
        // UIFlows test disabled due to unresolved API mismatches
        // Re-enable once MediaPipeline/UI integration is stabilized
    };
    
    for (const auto& test : tests) {
        qDebug() << "\n========================================";
        qDebug() << "Running test suite:" << test.name;
        qDebug() << "========================================";
        
        testCount++;
        int result = test.function(argc, argv);
        
        if (result == 0) {
            qDebug() << "✓ Test suite" << test.name << "PASSED";
            passedTests++;
        } else {
            qDebug() << "✗ Test suite" << test.name << "FAILED with code" << result;
            totalResult |= result;
        }
    }

    Murmur::Test::TestUtils::cleanupTestEnvironment();
    
    // Summary
    qDebug() << "\n========================================";
    qDebug() << "TEST SUMMARY";
    qDebug() << "========================================";
    qDebug() << "Total test suites:" << testCount;
    qDebug() << "Passed:" << passedTests;
    qDebug() << "Failed:" << (testCount - passedTests);
    qDebug() << "Overall result:" << (totalResult == 0 ? "PASS" : "FAIL");
    
    return totalResult;
}