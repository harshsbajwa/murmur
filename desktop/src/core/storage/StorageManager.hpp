#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QMutex>
#include <QDateTime>
#include <QVariant>
#include <QJsonObject>
#include <QRegularExpression>
#include "../common/Expected.hpp"
#include "../common/RetryManager.hpp"
#include "../common/ErrorRecovery.hpp"

namespace Murmur {

enum class StorageError {
    DatabaseNotOpen,
    ConnectionFailed,
    QueryFailed,
    DataNotFound,
    InvalidData,
    ConstraintViolation,
    DiskSpaceError,
    PermissionDenied,
    TransactionFailed,
    MigrationFailed
};

struct TorrentRecord {
    QString infoHash;
    QString name;
    QString magnetUri;
    qint64 size;
    QDateTime dateAdded;
    QDateTime lastActive;
    QString savePath;
    double progress;
    QString status;  // "downloading", "seeding", "paused", "error"
    QJsonObject metadata;
    QStringList files;
    int seeders;
    int leechers;
    qint64 downloaded;
    qint64 uploaded;
    double ratio;
};

struct MediaRecord {
    QString id;
    QString torrentHash;
    QString filePath;
    QString originalName;
    QString mimeType;
    qint64 fileSize;
    qint64 duration;  // milliseconds
    int width;
    int height;
    double frameRate;
    QString videoCodec;
    QString audioCodec;
    bool hasTranscription;
    QDateTime dateAdded;
    QDateTime lastPlayed;
    qint64 playbackPosition;  // milliseconds
    QJsonObject metadata;
    
    // Validation method
    bool isValid() const {
        // Check required fields
        if (id.isEmpty() || id.length() > 255) {
            return false;
        }
        
        if (filePath.isEmpty() || originalName.isEmpty()) {
            return false;
        }
        
        // Check numeric constraints
        if (fileSize < 0 || duration < 0 || width < 0 || height < 0 || frameRate < 0.0 || playbackPosition < 0) {
            return false;
        }
        
        // Check torrentHash format if it's provided
        if (!torrentHash.isEmpty() && (torrentHash.length() != 40 || !torrentHash.contains(QRegularExpression("^[0-9a-fA-F]{40}$")))) {
            return false;
        }
        
        return true;
    }
};

struct TranscriptionRecord {
    QString id;
    QString mediaId;
    QString language;
    QString modelUsed;
    QString fullText;
    QJsonObject timestamps;  // Segment timestamps and text
    double confidence;
    QDateTime dateCreated;
    qint64 processingTime;
    QString status;  // "processing", "completed", "failed"
};

struct PlaybackSession {
    QString sessionId;
    QString mediaId;
    QDateTime startTime;
    QDateTime endTime;
    qint64 startPosition;
    qint64 endPosition;
    qint64 totalDuration;
    bool completed;
};

/**
 * @brief SQLite-based storage manager for application data
 * 
 * Manages torrents, media files, transcriptions, and playback history
 * with ACID transactions and comprehensive error handling.
 */
class StorageManager : public QObject {
    Q_OBJECT

public:
    explicit StorageManager(QObject* parent = nullptr);
    ~StorageManager();

    // Database lifecycle
    Expected<bool, StorageError> initialize(const QString& databasePath = QString());
    void close();
    bool isOpen() const;
    
    // Transaction management
    Expected<bool, StorageError> beginTransaction();
    Expected<bool, StorageError> commitTransaction();
    Expected<bool, StorageError> rollbackTransaction();
    
    // Torrent operations
    Expected<bool, StorageError> addTorrent(const TorrentRecord& torrent);
    Expected<bool, StorageError> updateTorrent(const TorrentRecord& torrent);
    Expected<bool, StorageError> removeTorrent(const QString& infoHash);
    Expected<TorrentRecord, StorageError> getTorrent(const QString& infoHash);
    Expected<QList<TorrentRecord>, StorageError> getAllTorrents();
    Expected<QList<TorrentRecord>, StorageError> getActiveTorrents();
    Expected<bool, StorageError> updateTorrentProgress(const QString& infoHash, double progress);
    Expected<bool, StorageError> updateTorrentStatus(const QString& infoHash, const QString& status);
    
    // Media operations
    Expected<QString, StorageError> addMedia(const MediaRecord& media);
    Expected<bool, StorageError> updateMedia(const MediaRecord& media);
    Expected<bool, StorageError> removeMedia(const QString& mediaId);
    Expected<MediaRecord, StorageError> getMedia(const QString& mediaId);
    Expected<QList<MediaRecord>, StorageError> getMediaByTorrent(const QString& torrentHash);
    Expected<QList<MediaRecord>, StorageError> getAllMedia();
    Expected<bool, StorageError> updatePlaybackPosition(const QString& mediaId, qint64 position);
    Expected<QList<MediaRecord>, StorageError> getRecentMedia(int limit = 20);
    
    // Transcription operations
    Expected<QString, StorageError> addTranscription(const TranscriptionRecord& transcription);
    Expected<bool, StorageError> updateTranscription(const TranscriptionRecord& transcription);
    Expected<bool, StorageError> removeTranscription(const QString& transcriptionId);
    Expected<TranscriptionRecord, StorageError> getTranscription(const QString& transcriptionId);
    Expected<TranscriptionRecord, StorageError> getTranscriptionByMedia(const QString& mediaId);
    Expected<QList<TranscriptionRecord>, StorageError> getAllTranscriptions();
    Expected<bool, StorageError> updateTranscriptionStatus(const QString& transcriptionId, const QString& status);
    
    // Playback history
    Expected<QString, StorageError> recordPlaybackSession(const PlaybackSession& session);
    Expected<bool, StorageError> updatePlaybackSession(const PlaybackSession& session);
    Expected<QList<PlaybackSession>, StorageError> getPlaybackHistory(const QString& mediaId, int limit = 10);
    Expected<bool, StorageError> markSessionCompleted(const QString& sessionId);
    Expected<bool, StorageError> clearPlaybackPositions();
    
    // Search and filtering
    Expected<QList<TorrentRecord>, StorageError> searchTorrents(const QString& query);
    Expected<QList<MediaRecord>, StorageError> searchMedia(const QString& query);
    Expected<QList<TranscriptionRecord>, StorageError> searchTranscriptions(const QString& query);
    
    // Statistics and analytics
    Expected<QJsonObject, StorageError> getTorrentStatistics();
    Expected<QJsonObject, StorageError> getMediaStatistics();
    Expected<QJsonObject, StorageError> getPlaybackStatistics();
    Expected<qint64, StorageError> getTotalStorageUsed();
    
    // Maintenance operations
    Expected<bool, StorageError> vacuum();
    Expected<bool, StorageError> reindex();
    Expected<bool, StorageError> cleanupOrphanedRecords();
    Expected<bool, StorageError> backupDatabase(const QString& backupPath);
    Expected<bool, StorageError> restoreDatabase(const QString& backupPath);
    
    // Configuration
    void setAutoCommit(bool autoCommit);
    void setCacheSize(int sizeMB);
    void setJournalMode(const QString& mode);  // "WAL", "DELETE", "TRUNCATE"
    Expected<int, StorageError> getSchemaVersion() const;
    
    // Testing support
    Expected<bool, StorageError> testMigrateDatabase();

signals:
    void torrentAdded(const QString& infoHash);
    void torrentUpdated(const QString& infoHash);
    void torrentRemoved(const QString& infoHash);
    void mediaAdded(const QString& mediaId);
    void mediaUpdated(const QString& mediaId);
    void transcriptionCompleted(const QString& mediaId);
    void databaseError(StorageError error, const QString& description);

private slots:
    void onDatabaseError();

private:
    // Database initialization
    Expected<bool, StorageError> createTables();
    Expected<bool, StorageError> migrateDatabase();
    Expected<bool, StorageError> validateSchema();
    
    // SQL helpers
    Expected<QSqlQuery, StorageError> prepareQuery(const QString& sql);
    Expected<bool, StorageError> executeQuery(QSqlQuery& query);
    Expected<QVariant, StorageError> executeScalar(const QString& sql, const QVariantList& params = {}) const;
    
    // Record conversion
    TorrentRecord torrentFromQuery(const QSqlQuery& query);
    MediaRecord mediaFromQuery(const QSqlQuery& query);
    TranscriptionRecord transcriptionFromQuery(const QSqlQuery& query);
    PlaybackSession sessionFromQuery(const QSqlQuery& query);
    
    // Parameter binding
    void bindTorrentParams(QSqlQuery& query, const TorrentRecord& torrent);
    void bindMediaParams(QSqlQuery& query, const MediaRecord& media);
    void bindTranscriptionParams(QSqlQuery& query, const TranscriptionRecord& transcription);
    void bindSessionParams(QSqlQuery& query, const PlaybackSession& session);
    
    // Validation
    Expected<bool, StorageError> validateTorrentRecord(const TorrentRecord& torrent);
    Expected<bool, StorageError> validateMediaRecord(const MediaRecord& media);
    Expected<bool, StorageError> validateTranscriptionRecord(const TranscriptionRecord& transcription);
    
    // Migration helpers
    Expected<bool, StorageError> performMigration(int targetVersion);
    
    // Utility functions
    QString generateId();
    QString sanitizeQuery(const QString& query);
    StorageError mapSqlError(const QSqlError& error);
    
    // Error handling and recovery methods
    void setupErrorRecoveryStrategies();
    Expected<bool, StorageError> initializeDatabaseWithRetry(const QString& databasePath);
    
    QSqlDatabase database_;
    mutable QMutex databaseMutex_;
    QString connectionName_;
    bool autoCommit_;
    bool inTransaction_;
    
    // Error handling and recovery
    std::unique_ptr<ErrorRecovery> errorRecovery_;
    std::unique_ptr<RetryManager> retryManager_;
    
    // SQL statements (prepared for performance)
    QString sqlInsertTorrent_;
    QString sqlUpdateTorrent_;
    QString sqlSelectTorrent_;
    QString sqlDeleteTorrent_;
    QString sqlInsertMedia_;
    QString sqlUpdateMedia_;
    QString sqlSelectMedia_;
    QString sqlDeleteMedia_;
    QString sqlInsertTranscription_;
    QString sqlUpdateTranscription_;
    QString sqlSelectTranscription_;
    QString sqlDeleteTranscription_;
    QString sqlInsertSession_;
    QString sqlUpdateSession_;
    QString sqlSelectSession_;
    
    // Configuration
    static const int DEFAULT_CACHE_SIZE_MB = 64;
    static const QString DEFAULT_JOURNAL_MODE;
    static const int CURRENT_SCHEMA_VERSION = 1;
    
    // Private helper methods
    bool applyMigration(int toVersion);
};

} // namespace Murmur