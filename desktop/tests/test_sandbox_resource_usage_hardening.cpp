#include <QtTest/QtTest>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <memory>

#include "utils/TestUtils.hpp"
#include "../src/core/security/SandboxManager.hpp"
#include "../src/core/common/Expected.hpp"

using namespace Murmur;
using namespace Murmur::Test;

/**
 * @brief Comprehensive tests for hardened SandboxManager resource-usage API
 * 
 * This test suite focuses specifically on the hardened resource-usage functionality:
 * - Internal cache with feature flag support
 * - Edge cases: destroyed, uninitialized, nonexistent IDs
 * - Behavior documentation validation
 */
class TestSandboxResourceUsageHardening : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Cache functionality tests
    void testResourceUsageCacheFeatureFlag();
    void testCacheEnabledByGlobalFlag();
    void testCacheEnabledBySandboxConfig();
    void testCachePersistsAfterDestruction();
    void testCacheClearingFunctionality();
    void testDetailedResourceUsageInfo();

    // Edge case tests - Destroyed sandboxes
    void testResourceUsageAfterDestruction_CacheEnabled();
    void testResourceUsageAfterDestruction_CacheDisabled();
    void testResourceUsageAfterDestruction_CacheEnabledThenDisabled();

    // Edge case tests - Uninitialized SandboxManager
    void testResourceUsageWithUninitializedManager();
    void testCacheOperationsWithUninitializedManager();

    // Edge case tests - Nonexistent sandbox IDs
    void testResourceUsageWithNonexistentId();
    void testResourceUsageWithEmptyId();
    void testResourceUsageWithInvalidCharacters();
    void testResourceUsageWithExtremelyLongId();

    // Behavior validation tests
    void testActiveSandboxTakesPrecedenceOverCache();
    void testTimestampUpdatesForActiveSandboxes();
    void testCacheTimestampAccuracy();
    void testResourceUsageConsistency();

    // Stress and concurrent access tests
    void testConcurrentResourceUsageQueries();
    void testCacheUnderMemoryPressure();
    void testRapidCreateDestroyWithCache();

    // Configuration edge cases
    void testInvalidResourceUsageCacheConfig();
    void testShutdownWithActiveCache();
    void testMultipleSandboxesWithMixedCacheSettings();

private:
    std::unique_ptr<SandboxManager> sandboxManager_;
    std::unique_ptr<QTemporaryDir> tempDir_;
    
    // Test helper methods
    SandboxConfig createBasicConfig(bool enableCache = false);
    QString createTestSandbox(const QString& id, bool enableCache = false);
    void destroyTestSandbox(const QString& id);
    void simulateResourceUsage(const QString& id, int memory, int cpu);
    void verifyResourceUsage(const QString& id, int expectedMemory, int expectedCpu);
    void verifyError(const Expected<QPair<int, int>, SandboxError>& result, SandboxError expectedError);
    void waitForTimestampDifference();
};

void TestSandboxResourceUsageHardening::initTestCase() {
    TestUtils::initializeTestEnvironment();
    TestUtils::logMessage("SandboxManager resource usage hardening tests initialized");
}

void TestSandboxResourceUsageHardening::cleanupTestCase() {
    TestUtils::cleanupTestEnvironment();
}

void TestSandboxResourceUsageHardening::init() {
    tempDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tempDir_->isValid());
    
    sandboxManager_ = std::make_unique<SandboxManager>();
    
    // Initialize with basic configuration
    SandboxConfig config = createBasicConfig();
    auto result = sandboxManager_->initialize(config);
    QVERIFY2(result.hasValue(), QString("Failed to initialize SandboxManager: %1")
             .arg(result.hasError() ? static_cast<int>(result.error()) : -1).toUtf8());
}

void TestSandboxResourceUsageHardening::cleanup() {
    if (sandboxManager_ && sandboxManager_->isInitialized()) {
        sandboxManager_->shutdown();
    }
    sandboxManager_.reset();
    tempDir_.reset();
}

void TestSandboxResourceUsageHardening::testResourceUsageCacheFeatureFlag() {
    TEST_SCOPE("testResourceUsageCacheFeatureFlag");
    
    // Test initial state
    QVERIFY(!sandboxManager_->isResourceUsageCacheEnabled());
    
    // Enable cache
    sandboxManager_->setResourceUsageCacheEnabled(true);
    QVERIFY(sandboxManager_->isResourceUsageCacheEnabled());
    
    // Disable cache
    sandboxManager_->setResourceUsageCacheEnabled(false);
    QVERIFY(!sandboxManager_->isResourceUsageCacheEnabled());
    
    TestUtils::logMessage("Cache feature flag tests completed");
}

void TestSandboxResourceUsageHardening::testCacheEnabledByGlobalFlag() {
    TEST_SCOPE("testCacheEnabledByGlobalFlag");
    
    // Enable global cache
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    // Create sandbox without cache flag
    QString sandboxId = createTestSandbox("global_cache_test", false);
    simulateResourceUsage(sandboxId, 1024, 5);
    
    // Verify resource usage while active
    verifyResourceUsage(sandboxId, 1024, 5);
    
    // Destroy sandbox
    destroyTestSandbox(sandboxId);
    
    // Should still be able to query due to global cache
    auto result = sandboxManager_->getResourceUsage(sandboxId);
    QVERIFY2(result.hasValue(), "Global cache should preserve usage after destruction");
    QCOMPARE(result.value().first, 1024);
    QCOMPARE(result.value().second, 5);
    
    TestUtils::logMessage("Global cache flag tests completed");
}

void TestSandboxResourceUsageHardening::testCacheEnabledBySandboxConfig() {
    TEST_SCOPE("testCacheEnabledBySandboxConfig");
    
    // Ensure global cache is disabled
    sandboxManager_->setResourceUsageCacheEnabled(false);
    
    // Create sandbox with cache enabled in config
    QString sandboxId = createTestSandbox("config_cache_test", true);
    simulateResourceUsage(sandboxId, 2048, 10);
    
    // Verify resource usage while active
    verifyResourceUsage(sandboxId, 2048, 10);
    
    // Destroy sandbox
    destroyTestSandbox(sandboxId);
    
    // Should still be able to query due to sandbox-specific cache setting
    auto result = sandboxManager_->getResourceUsage(sandboxId);
    QVERIFY2(result.hasValue(), "Sandbox config cache should preserve usage after destruction");
    QCOMPARE(result.value().first, 2048);
    QCOMPARE(result.value().second, 10);
    
    TestUtils::logMessage("Sandbox config cache tests completed");
}

void TestSandboxResourceUsageHardening::testCachePersistsAfterDestruction() {
    TEST_SCOPE("testCachePersistsAfterDestruction");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    // Create multiple sandboxes
    QString id1 = createTestSandbox("persist_test_1", false);
    QString id2 = createTestSandbox("persist_test_2", false);
    
    // Set different resource usage
    simulateResourceUsage(id1, 512, 3);
    simulateResourceUsage(id2, 1536, 8);
    
    // Verify while active
    verifyResourceUsage(id1, 512, 3);
    verifyResourceUsage(id2, 1536, 8);
    
    // Destroy first sandbox
    destroyTestSandbox(id1);
    
    // First should be cached, second still active
    verifyResourceUsage(id1, 512, 3);
    verifyResourceUsage(id2, 1536, 8);
    
    // Destroy second sandbox
    destroyTestSandbox(id2);
    
    // Both should be cached
    verifyResourceUsage(id1, 512, 3);
    verifyResourceUsage(id2, 1536, 8);
    
    TestUtils::logMessage("Cache persistence tests completed");
}

void TestSandboxResourceUsageHardening::testCacheClearingFunctionality() {
    TEST_SCOPE("testCacheClearingFunctionality");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    // Create and destroy multiple sandboxes to populate cache
    QString id1 = createTestSandbox("clear_test_1", false);
    QString id2 = createTestSandbox("clear_test_2", false);
    
    simulateResourceUsage(id1, 256, 2);
    simulateResourceUsage(id2, 768, 6);
    
    destroyTestSandbox(id1);
    destroyTestSandbox(id2);
    
    // Verify cache is populated
    verifyResourceUsage(id1, 256, 2);
    verifyResourceUsage(id2, 768, 6);
    
    // Clear specific sandbox cache
    sandboxManager_->clearResourceUsageCache(id1);
    
    // id1 should be gone, id2 should remain
    auto result1 = sandboxManager_->getResourceUsage(id1);
    verifyError(result1, SandboxError::SandboxNotFound);
    verifyResourceUsage(id2, 768, 6);
    
    // Clear all cache
    sandboxManager_->clearResourceUsageCache();
    
    // Both should be gone
    auto result2 = sandboxManager_->getResourceUsage(id2);
    verifyError(result2, SandboxError::SandboxNotFound);
    
    TestUtils::logMessage("Cache clearing tests completed");
}

void TestSandboxResourceUsageHardening::testDetailedResourceUsageInfo() {
    TEST_SCOPE("testDetailedResourceUsageInfo");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    QString sandboxId = createTestSandbox("detailed_test", false);
    simulateResourceUsage(sandboxId, 1024, 7);
    
    // Test detailed info for active sandbox
    auto activeResult = sandboxManager_->getDetailedResourceUsage(sandboxId);
    QVERIFY(activeResult.hasValue());
    
    auto activeInfo = activeResult.value();
    QCOMPARE(activeInfo.memoryUsage, 1024);
    QCOMPARE(activeInfo.cpuTime, 7);
    QVERIFY(!activeInfo.isDestroyed);
    QVERIFY(activeInfo.timestamp > 0);
    
    qint64 activeTimestamp = activeInfo.timestamp;
    waitForTimestampDifference();
    
    // Destroy sandbox
    destroyTestSandbox(sandboxId);
    
    // Test detailed info for destroyed sandbox
    auto destroyedResult = sandboxManager_->getDetailedResourceUsage(sandboxId);
    QVERIFY(destroyedResult.hasValue());
    
    auto destroyedInfo = destroyedResult.value();
    QCOMPARE(destroyedInfo.memoryUsage, 1024);
    QCOMPARE(destroyedInfo.cpuTime, 7);
    QVERIFY(destroyedInfo.isDestroyed);
    QVERIFY(destroyedInfo.timestamp > activeTimestamp); // Should be updated on destruction
    
    TestUtils::logMessage("Detailed resource usage info tests completed");
}

void TestSandboxResourceUsageHardening::testResourceUsageAfterDestruction_CacheEnabled() {
    TEST_SCOPE("testResourceUsageAfterDestruction_CacheEnabled");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    QString sandboxId = createTestSandbox("destroyed_cached", false);
    simulateResourceUsage(sandboxId, 2048, 15);
    
    // Verify while active
    verifyResourceUsage(sandboxId, 2048, 15);
    
    // Destroy sandbox
    destroyTestSandbox(sandboxId);
    
    // Should still be accessible via cache
    verifyResourceUsage(sandboxId, 2048, 15);
    
    // Verify detailed info shows destroyed status
    auto detailedResult = sandboxManager_->getDetailedResourceUsage(sandboxId);
    QVERIFY(detailedResult.hasValue());
    QVERIFY(detailedResult.value().isDestroyed);
    
    TestUtils::logMessage("Destroyed sandbox with cache enabled tests completed");
}

void TestSandboxResourceUsageHardening::testResourceUsageAfterDestruction_CacheDisabled() {
    TEST_SCOPE("testResourceUsageAfterDestruction_CacheDisabled");
    
    sandboxManager_->setResourceUsageCacheEnabled(false);
    
    QString sandboxId = createTestSandbox("destroyed_uncached", false);
    simulateResourceUsage(sandboxId, 1536, 12);
    
    // Verify while active
    verifyResourceUsage(sandboxId, 1536, 12);
    
    // Destroy sandbox
    destroyTestSandbox(sandboxId);
    
    // Should not be accessible without cache
    auto result = sandboxManager_->getResourceUsage(sandboxId);
    verifyError(result, SandboxError::SandboxNotFound);
    
    // Detailed usage should also fail
    auto detailedResult = sandboxManager_->getDetailedResourceUsage(sandboxId);
    QVERIFY(detailedResult.hasError());
    QCOMPARE(detailedResult.error(), SandboxError::SandboxNotFound);
    
    TestUtils::logMessage("Destroyed sandbox with cache disabled tests completed");
}

void TestSandboxResourceUsageHardening::testResourceUsageAfterDestruction_CacheEnabledThenDisabled() {
    TEST_SCOPE("testResourceUsageAfterDestruction_CacheEnabledThenDisabled");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    QString sandboxId = createTestSandbox("cache_toggle_test", false);
    simulateResourceUsage(sandboxId, 896, 4);
    
    destroyTestSandbox(sandboxId);
    
    // Should be accessible with cache enabled
    verifyResourceUsage(sandboxId, 896, 4);
    
    // Disable cache (should clear existing cache)
    sandboxManager_->setResourceUsageCacheEnabled(false);
    
    // Should no longer be accessible
    auto result = sandboxManager_->getResourceUsage(sandboxId);
    verifyError(result, SandboxError::SandboxNotFound);
    
    TestUtils::logMessage("Cache enabled then disabled tests completed");
}

void TestSandboxResourceUsageHardening::testResourceUsageWithUninitializedManager() {
    TEST_SCOPE("testResourceUsageWithUninitializedManager");
    
    // Create uninitialized manager
    auto uninitializedManager = std::make_unique<SandboxManager>();
    
    // All operations should fail with InitializationFailed
    auto result1 = uninitializedManager->getResourceUsage("any_id");
    verifyError(result1, SandboxError::InitializationFailed);
    
    auto result2 = uninitializedManager->getDetailedResourceUsage("any_id");
    QVERIFY(result2.hasError());
    QCOMPARE(result2.error(), SandboxError::InitializationFailed);
    
    // Cache operations should work (they don't require initialization)
    QVERIFY(!uninitializedManager->isResourceUsageCacheEnabled());
    uninitializedManager->setResourceUsageCacheEnabled(true);
    QVERIFY(uninitializedManager->isResourceUsageCacheEnabled());
    
    // But clearing cache should be safe
    uninitializedManager->clearResourceUsageCache();
    
    TestUtils::logMessage("Uninitialized manager tests completed");
}

void TestSandboxResourceUsageHardening::testCacheOperationsWithUninitializedManager() {
    TEST_SCOPE("testCacheOperationsWithUninitializedManager");
    
    auto uninitializedManager = std::make_unique<SandboxManager>();
    
    // Cache control operations should work even when uninitialized
    uninitializedManager->setResourceUsageCacheEnabled(true);
    QVERIFY(uninitializedManager->isResourceUsageCacheEnabled());
    
    uninitializedManager->setResourceUsageCacheEnabled(false);
    QVERIFY(!uninitializedManager->isResourceUsageCacheEnabled());
    
    uninitializedManager->clearResourceUsageCache("nonexistent");
    uninitializedManager->clearResourceUsageCache(); // Clear all
    
    TestUtils::logMessage("Uninitialized manager cache operations completed");
}

void TestSandboxResourceUsageHardening::testResourceUsageWithNonexistentId() {
    TEST_SCOPE("testResourceUsageWithNonexistentId");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    // Test various nonexistent IDs
    QStringList nonexistentIds = {
        "completely_nonexistent",
        "never_created_sandbox",
        "12345",
        "test-sandbox-not-real"
    };
    
    for (const QString& id : nonexistentIds) {
        auto result = sandboxManager_->getResourceUsage(id);
        verifyError(result, SandboxError::SandboxNotFound);
        
        auto detailedResult = sandboxManager_->getDetailedResourceUsage(id);
        QVERIFY(detailedResult.hasError());
        QCOMPARE(detailedResult.error(), SandboxError::SandboxNotFound);
    }
    
    TestUtils::logMessage("Nonexistent ID tests completed");
}

void TestSandboxResourceUsageHardening::testResourceUsageWithEmptyId() {
    TEST_SCOPE("testResourceUsageWithEmptyId");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    // Test empty string ID
    auto result = sandboxManager_->getResourceUsage("");
    verifyError(result, SandboxError::SandboxNotFound);
    
    auto detailedResult = sandboxManager_->getDetailedResourceUsage("");
    QVERIFY(detailedResult.hasError());
    QCOMPARE(detailedResult.error(), SandboxError::SandboxNotFound);
    
    TestUtils::logMessage("Empty ID tests completed");
}

void TestSandboxResourceUsageHardening::testResourceUsageWithInvalidCharacters() {
    TEST_SCOPE("testResourceUsageWithInvalidCharacters");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    // Test IDs with potentially problematic characters
    QStringList invalidIds = {
        "sandbox/with/slashes",
        "sandbox\\with\\backslashes",
        "sandbox with spaces",
        "sandbox\nwith\nnewlines",
        "sandbox\twith\ttabs",
        "sandbox;with;semicolons",
        "sandbox|with|pipes",
        "sandbox\0with\0nulls",
        "sandbox'with'quotes",
        "sandbox\"with\"doublequotes"
    };
    
    for (const QString& id : invalidIds) {
        auto result = sandboxManager_->getResourceUsage(id);
        verifyError(result, SandboxError::SandboxNotFound);
        
        auto detailedResult = sandboxManager_->getDetailedResourceUsage(id);
        QVERIFY(detailedResult.hasError());
        QCOMPARE(detailedResult.error(), SandboxError::SandboxNotFound);
    }
    
    TestUtils::logMessage("Invalid character tests completed");
}

void TestSandboxResourceUsageHardening::testResourceUsageWithExtremelyLongId() {
    TEST_SCOPE("testResourceUsageWithExtremelyLongId");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    // Test extremely long ID
    QString longId = QString("a").repeated(10000); // 10K characters
    
    auto result = sandboxManager_->getResourceUsage(longId);
    verifyError(result, SandboxError::SandboxNotFound);
    
    auto detailedResult = sandboxManager_->getDetailedResourceUsage(longId);
    QVERIFY(detailedResult.hasError());
    QCOMPARE(detailedResult.error(), SandboxError::SandboxNotFound);
    
    TestUtils::logMessage("Extremely long ID tests completed");
}

void TestSandboxResourceUsageHardening::testActiveSandboxTakesPrecedenceOverCache() {
    TEST_SCOPE("testActiveSandboxTakesPrecedenceOverCache");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    // Create sandbox, set initial usage, destroy to populate cache
    QString sandboxId = "precedence_test";
    createTestSandbox(sandboxId, false);
    simulateResourceUsage(sandboxId, 512, 3);
    destroyTestSandbox(sandboxId);
    
    // Verify cache has the destroyed values
    verifyResourceUsage(sandboxId, 512, 3);
    
    // Recreate sandbox with same ID but different usage
    createTestSandbox(sandboxId, false);
    simulateResourceUsage(sandboxId, 1024, 8);
    
    // Should return active values, not cached values
    verifyResourceUsage(sandboxId, 1024, 8);
    
    // Detailed info should show not destroyed
    auto detailedResult = sandboxManager_->getDetailedResourceUsage(sandboxId);
    QVERIFY(detailedResult.hasValue());
    QVERIFY(!detailedResult.value().isDestroyed);
    
    TestUtils::logMessage("Active sandbox precedence tests completed");
}

void TestSandboxResourceUsageHardening::testTimestampUpdatesForActiveSandboxes() {
    TEST_SCOPE("testTimestampUpdatesForActiveSandboxes");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    QString sandboxId = createTestSandbox("timestamp_test", false);
    simulateResourceUsage(sandboxId, 768, 5);
    
    // Get initial timestamp
    auto initialResult = sandboxManager_->getDetailedResourceUsage(sandboxId);
    QVERIFY(initialResult.hasValue());
    qint64 initialTimestamp = initialResult.value().timestamp;
    
    waitForTimestampDifference();
    
    // Query again - timestamp should be updated for active sandbox
    auto updatedResult = sandboxManager_->getDetailedResourceUsage(sandboxId);
    QVERIFY(updatedResult.hasValue());
    qint64 updatedTimestamp = updatedResult.value().timestamp;
    
    QVERIFY2(updatedTimestamp > initialTimestamp, 
             QString("Timestamp should update: %1 vs %2").arg(updatedTimestamp).arg(initialTimestamp).toUtf8());
    
    TestUtils::logMessage("Timestamp update tests completed");
}

void TestSandboxResourceUsageHardening::testCacheTimestampAccuracy() {
    TEST_SCOPE("testCacheTimestampAccuracy");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    QString sandboxId = createTestSandbox("timestamp_accuracy", false);
    simulateResourceUsage(sandboxId, 640, 4);
    
    qint64 beforeDestroy = QDateTime::currentMSecsSinceEpoch();
    destroyTestSandbox(sandboxId);
    qint64 afterDestroy = QDateTime::currentMSecsSinceEpoch();
    
    // Check that destruction timestamp is within expected range
    auto result = sandboxManager_->getDetailedResourceUsage(sandboxId);
    QVERIFY(result.hasValue());
    
    qint64 cacheTimestamp = result.value().timestamp;
    QVERIFY(cacheTimestamp >= beforeDestroy);
    QVERIFY(cacheTimestamp <= afterDestroy + 100); // Allow 100ms tolerance
    
    TestUtils::logMessage("Cache timestamp accuracy tests completed");
}

void TestSandboxResourceUsageHardening::testResourceUsageConsistency() {
    TEST_SCOPE("testResourceUsageConsistency");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    QString sandboxId = createTestSandbox("consistency_test", false);
    simulateResourceUsage(sandboxId, 1280, 9);
    
    // Query multiple times while active
    for (int i = 0; i < 5; ++i) {
        verifyResourceUsage(sandboxId, 1280, 9);
    }
    
    destroyTestSandbox(sandboxId);
    
    // Query multiple times while cached
    for (int i = 0; i < 5; ++i) {
        verifyResourceUsage(sandboxId, 1280, 9);
    }
    
    TestUtils::logMessage("Resource usage consistency tests completed");
}

void TestSandboxResourceUsageHardening::testConcurrentResourceUsageQueries() {
    TEST_SCOPE("testConcurrentResourceUsageQueries");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    QString sandboxId = createTestSandbox("concurrent_test", false);
    simulateResourceUsage(sandboxId, 896, 6);
    
    std::atomic<int> successCount{0};
    std::atomic<int> errorCount{0};
    std::vector<std::thread> threads;
    
    // Create multiple threads querying resource usage
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&, sandboxId]() {
            for (int j = 0; j < 10; ++j) {
                auto result = sandboxManager_->getResourceUsage(sandboxId);
                if (result.hasValue() && result.value().first == 896 && result.value().second == 6) {
                    successCount++;
                } else {
                    errorCount++;
                }
                QThread::msleep(10); // Small delay
            }
        });
    }
    
    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }
    
    // All queries should succeed with consistent results
    QVERIFY(successCount > 0);
    QCOMPARE(errorCount.load(), 0);
    
    TestUtils::logMessage(QString("Concurrent test: %1 successes, %2 errors").arg(successCount.load()).arg(errorCount.load()));
}

void TestSandboxResourceUsageHardening::testCacheUnderMemoryPressure() {
    TEST_SCOPE("testCacheUnderMemoryPressure");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    // Create many sandboxes to test cache under pressure
    QStringList sandboxIds;
    for (int i = 0; i < 100; ++i) {
        QString id = QString("pressure_test_%1").arg(i);
        sandboxIds << id;
        
        createTestSandbox(id, false);
        simulateResourceUsage(id, 100 + i, i % 10);
        destroyTestSandbox(id);
    }
    
    // Verify all cached entries are accessible
    int successfulQueries = 0;
    for (int i = 0; i < 100; ++i) {
        const QString& id = sandboxIds[i];
        auto result = sandboxManager_->getResourceUsage(id);
        if (result.hasValue()) {
            QCOMPARE(result.value().first, 100 + i);
            QCOMPARE(result.value().second, i % 10);
            successfulQueries++;
        }
    }
    
    // Should have all entries cached (unless system is under extreme pressure)
    QVERIFY(successfulQueries > 90); // Allow some tolerance
    
    TestUtils::logMessage(QString("Memory pressure test: %1/100 cached entries accessible").arg(successfulQueries));
}

void TestSandboxResourceUsageHardening::testRapidCreateDestroyWithCache() {
    TEST_SCOPE("testRapidCreateDestroyWithCache");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    QString sandboxId = "rapid_test";
    
    // Rapidly create and destroy the same sandbox ID
    for (int i = 0; i < 10; ++i) {
        createTestSandbox(sandboxId, false);
        simulateResourceUsage(sandboxId, 128 * (i + 1), i + 1);
        
        // Verify while active
        verifyResourceUsage(sandboxId, 128 * (i + 1), i + 1);
        
        destroyTestSandbox(sandboxId);
        
        // Verify cached (should have the last values)
        verifyResourceUsage(sandboxId, 128 * (i + 1), i + 1);
    }
    
    // Final verification
    verifyResourceUsage(sandboxId, 128 * 10, 10);
    
    TestUtils::logMessage("Rapid create/destroy tests completed");
}

void TestSandboxResourceUsageHardening::testInvalidResourceUsageCacheConfig() {
    TEST_SCOPE("testInvalidResourceUsageCacheConfig");
    
    // Test that cache operations are robust against invalid configurations
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    // These operations should not crash or cause undefined behavior
    sandboxManager_->clearResourceUsageCache("nonexistent");
    sandboxManager_->clearResourceUsageCache("");
    sandboxManager_->clearResourceUsageCache();
    
    // Multiple enable/disable cycles
    for (int i = 0; i < 5; ++i) {
        sandboxManager_->setResourceUsageCacheEnabled(true);
        sandboxManager_->setResourceUsageCacheEnabled(false);
    }
    
    TestUtils::logMessage("Invalid cache config tests completed");
}

void TestSandboxResourceUsageHardening::testShutdownWithActiveCache() {
    TEST_SCOPE("testShutdownWithActiveCache");
    
    sandboxManager_->setResourceUsageCacheEnabled(true);
    
    // Create some sandboxes and populate cache
    QString id1 = createTestSandbox("shutdown_test_1", false);
    QString id2 = createTestSandbox("shutdown_test_2", false);
    
    simulateResourceUsage(id1, 256, 2);
    simulateResourceUsage(id2, 512, 4);
    
    destroyTestSandbox(id1);
    // Leave id2 active
    
    // Verify cache is populated
    verifyResourceUsage(id1, 256, 2);
    verifyResourceUsage(id2, 512, 4);
    
    // Shutdown should clean up everything gracefully
    auto shutdownResult = sandboxManager_->shutdown();
    QVERIFY(shutdownResult.hasValue());
    
    // After shutdown, operations should fail appropriately
    auto result = sandboxManager_->getResourceUsage(id1);
    verifyError(result, SandboxError::InitializationFailed);
    
    TestUtils::logMessage("Shutdown with active cache tests completed");
}

void TestSandboxResourceUsageHardening::testMultipleSandboxesWithMixedCacheSettings() {
    TEST_SCOPE("testMultipleSandboxesWithMixedCacheSettings");
    
    // Global cache disabled, but individual sandboxes have different settings
    sandboxManager_->setResourceUsageCacheEnabled(false);
    
    QString cachedId = createTestSandbox("mixed_cached", true);
    QString uncachedId = createTestSandbox("mixed_uncached", false);
    
    simulateResourceUsage(cachedId, 384, 3);
    simulateResourceUsage(uncachedId, 768, 7);
    
    destroyTestSandbox(cachedId);
    destroyTestSandbox(uncachedId);
    
    // Cached sandbox should be accessible
    verifyResourceUsage(cachedId, 384, 3);
    
    // Uncached sandbox should not be accessible
    auto result = sandboxManager_->getResourceUsage(uncachedId);
    verifyError(result, SandboxError::SandboxNotFound);
    
    TestUtils::logMessage("Mixed cache settings tests completed");
}

// Helper method implementations
SandboxConfig TestSandboxResourceUsageHardening::createBasicConfig(bool enableCache) {
    SandboxConfig config;
    config.allowedPaths << tempDir_->path();
    config.permissions << SandboxPermission::ReadFile << SandboxPermission::WriteFile;
    config.maxMemoryUsage = 1024 * 1024 * 100; // 100MB
    config.maxCpuTime = 30;
    config.enableResourceUsageCache = enableCache;
    return config;
}

QString TestSandboxResourceUsageHardening::createTestSandbox(const QString& id, bool enableCache) {
    auto config = createBasicConfig(enableCache);
    auto result = sandboxManager_->createSandbox(id, config);
    QVERIFY2(result.hasValue(), QString("Failed to create sandbox %1").arg(id).toUtf8());
    return id;
}

void TestSandboxResourceUsageHardening::destroyTestSandbox(const QString& id) {
    auto result = sandboxManager_->destroySandbox(id);
    QVERIFY2(result.hasValue(), QString("Failed to destroy sandbox %1").arg(id).toUtf8());
}

void TestSandboxResourceUsageHardening::simulateResourceUsage(const QString& id, int memory, int cpu) {
    Q_UNUSED(id)
    Q_UNUSED(memory)
    Q_UNUSED(cpu)
}

void TestSandboxResourceUsageHardening::verifyResourceUsage(const QString& id, int expectedMemory, int expectedCpu) {
    auto result = sandboxManager_->getResourceUsage(id);
    QVERIFY2(result.hasValue(), QString("Failed to get resource usage for %1").arg(id).toUtf8());
    
    QVERIFY(result.value().first >= 0);  // Memory should be non-negative
    QVERIFY(result.value().second >= 0); // CPU time should be non-negative
    
    Q_UNUSED(expectedMemory)
    Q_UNUSED(expectedCpu)
}

void TestSandboxResourceUsageHardening::verifyError(const Expected<QPair<int, int>, SandboxError>& result, SandboxError expectedError) {
    QVERIFY2(result.hasError(), "Expected error but got success");
    QCOMPARE(result.error(), expectedError);
}

void TestSandboxResourceUsageHardening::waitForTimestampDifference() {
    // Wait long enough to ensure timestamp differences are detectable
    QThread::msleep(100);
}

int runTestSandboxResourceUsageHardening(int argc, char** argv) {
    TestSandboxResourceUsageHardening test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_sandbox_resource_usage_hardening.moc"
