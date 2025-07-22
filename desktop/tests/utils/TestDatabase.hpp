#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTemporaryDir>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <memory>

#include "../../src/core/storage/StorageManager.hpp"
#include "../../src/core/common/Expected.hpp"

namespace Murmur {
namespace Test {

/**
 * @brief Test database utilities for comprehensive database testing
 * 
 * Provides test database creation, population, validation, and cleanup
 * for testing storage-related functionality.
 */
class TestDatabase : public QObject {
    Q_OBJECT

public:
    explicit TestDatabase(QObject* parent = nullptr);
    ~TestDatabase();

    // Database lifecycle
    Expected<QString, QString> createTestDatabase(const QString& name = "test");
    Expected<bool, QString> populateWithTestData(const QString& dbPath);
    Expected<bool, QString> validateDatabaseStructure(const QString& dbPath);
    void cleanupDatabase(const QString& dbPath);
    
    // Test data generation
    TorrentRecord createTestTorrentRecord(const QString& infoHash = QString());
    MediaRecord createTestMediaRecord(const QString& torrentHash = QString());
    TranscriptionRecord createTestTranscriptionRecord(const QString& mediaId = QString());
    PlaybackSession createTestPlaybackSession(const QString& mediaId = QString());
    
    // Bulk test data
    QList<TorrentRecord> createMultipleTorrentRecords(int count);
    QList<MediaRecord> createMultipleMediaRecords(int count, const QString& torrentHash = QString());
    QList<TranscriptionRecord> createMultipleTranscriptionRecords(int count);
    QList<PlaybackSession> createMultiplePlaybackSessions(int count);
    
    // Database validation
    Expected<int, QString> countRecords(const QString& dbPath, const QString& tableName);
    Expected<bool, QString> verifyRecordExists(const QString& dbPath, const QString& tableName, 
                                              const QString& keyColumn, const QString& keyValue);
    Expected<QVariantMap, QString> getRecordData(const QString& dbPath, const QString& tableName,
                                                 const QString& keyColumn, const QString& keyValue);
    
    // Performance testing utilities
    Expected<qint64, QString> measureInsertPerformance(const QString& dbPath, int recordCount);
    Expected<qint64, QString> measureQueryPerformance(const QString& dbPath, const QString& query);
    Expected<QJsonObject, QString> analyzeIndexUsage(const QString& dbPath);
    
    // Database stress testing
    Expected<bool, QString> performConcurrentWrites(const QString& dbPath, int threadCount, int operationsPerThread);
    Expected<bool, QString> performLongRunningTransaction(const QString& dbPath, int operationCount);
    Expected<bool, QString> testDatabaseRecovery(const QString& dbPath);
    
    // Migration testing
    Expected<bool, QString> createOldSchemaDatabase(const QString& dbPath, int schemaVersion);
    Expected<bool, QString> verifyMigrationResult(const QString& dbPath, int expectedVersion);
    
    // Data integrity testing
    Expected<bool, QString> verifyForeignKeyConstraints(const QString& dbPath);
    Expected<bool, QString> verifyUniqueConstraints(const QString& dbPath);
    Expected<bool, QString> checkDataConsistency(const QString& dbPath);
    
    // Backup and restore testing
    Expected<QString, QString> createDatabaseBackup(const QString& dbPath);
    Expected<bool, QString> restoreDatabaseFromBackup(const QString& backupPath, const QString& targetPath);
    Expected<bool, QString> verifyBackupIntegrity(const QString& backupPath, const QString& originalPath);

private:
    std::unique_ptr<QTemporaryDir> tempDir_;
    
    // Helper methods
    QString generateRandomString(int length = 10);
    QDateTime generateRandomDateTime();
    QJsonObject generateRandomMetadata();
    QString getNextAvailableId();
    Expected<bool, QString> executeQuery(QSqlDatabase& db, const QString& query, const QVariantList& params = {});
    Expected<QVariant, QString> executeScalarQuery(QSqlDatabase& db, const QString& query, const QVariantList& params = {});
    
    // Test data patterns
    QStringList sampleTorrentNames_;
    QStringList sampleVideoCodecs_;
    QStringList sampleAudioCodecs_;
    QStringList sampleLanguages_;
    QStringList sampleFileExtensions_;
    
    int recordIdCounter_;
};

/**
 * @brief RAII helper for test database lifecycle management
 */
class TestDatabaseScope {
public:
    explicit TestDatabaseScope(const QString& testName);
    ~TestDatabaseScope();
    
    QString getDatabasePath() const;
    TestDatabase* getDatabase() const;
    
    // Convenience methods
    StorageManager* createStorageManager();
    Expected<bool, QString> populateWithSampleData();
    Expected<QJsonObject, QString> getTestStatistics();

private:
    QString testName_;
    QString databasePath_;
    std::unique_ptr<TestDatabase> testDatabase_;
    std::unique_ptr<StorageManager> storageManager_;
};

/**
 * @brief Database performance benchmark helper
 */
class DatabaseBenchmark {
public:
    explicit DatabaseBenchmark(const QString& dbPath);
    
    // Benchmark operations
    QJsonObject benchmarkInserts(int recordCount);
    QJsonObject benchmarkSelects(int queryCount);
    QJsonObject benchmarkUpdates(int updateCount);
    QJsonObject benchmarkDeletes(int deleteCount);
    QJsonObject benchmarkComplexQueries(int queryCount);
    
    // Stress test operations
    QJsonObject stressTestConcurrentAccess(int threadCount, int operationsPerThread);
    QJsonObject stressTestLargeDatasets(int recordCount);
    QJsonObject stressTestLongRunningTransactions(int transactionCount);
    
    // Generate comprehensive report
    QJsonObject generatePerformanceReport();

private:
    QString databasePath_;
    std::unique_ptr<TestDatabase> testDatabase_;
    QJsonObject performanceMetrics_;
    
    qint64 measureOperation(std::function<void()> operation);
    void recordMetric(const QString& operation, const QString& metric, const QVariant& value);
};

// Convenience macros for database testing
#define TEST_DATABASE_SCOPE(name) TestDatabaseScope _dbScope(name)
#define ASSERT_DATABASE_OPERATION(result) \
    if (result.hasError()) { \
        QFAIL(qPrintable(QString("Database operation failed: %1").arg(result.error()))); \
    }

#define VERIFY_RECORD_COUNT(dbPath, table, expectedCount) \
    { \
        auto countResult = _dbScope.getDatabase()->countRecords(dbPath, table); \
        ASSERT_DATABASE_OPERATION(countResult); \
        QCOMPARE(countResult.value(), expectedCount); \
    }

#define VERIFY_RECORD_EXISTS(dbPath, table, keyColumn, keyValue) \
    { \
        auto existsResult = _dbScope.getDatabase()->verifyRecordExists(dbPath, table, keyColumn, keyValue); \
        ASSERT_DATABASE_OPERATION(existsResult); \
        QVERIFY(existsResult.value()); \
    }

} // namespace Test
} // namespace Murmur