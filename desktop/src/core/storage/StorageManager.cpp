#include "StorageManager.hpp"
#include "../common/Logger.hpp"
#include "../security/InputValidator.hpp"

#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QDebug>

namespace Murmur {

const QString StorageManager::DEFAULT_JOURNAL_MODE = "WAL";
const int StorageManager::CURRENT_SCHEMA_VERSION;

StorageManager::StorageManager(QObject* parent)
    : QObject(parent)
    , connectionName_(QString("MurmurDB_%1_%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(reinterpret_cast<quintptr>(this), 0, 16))
    , autoCommit_(true)
    , inTransaction_(false) {
    
    // Initialize SQL statements
    sqlInsertTorrent_ = R"(
        INSERT INTO torrents (info_hash, name, magnet_uri, size, date_added, 
                            last_active, save_path, progress, status, metadata, 
                            files, seeders, leechers, downloaded, uploaded, ratio)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlUpdateTorrent_ = R"(
        UPDATE torrents SET name = ?, magnet_uri = ?, size = ?, last_active = ?,
                          save_path = ?, progress = ?, status = ?, metadata = ?,
                          files = ?, seeders = ?, leechers = ?, downloaded = ?,
                          uploaded = ?, ratio = ?
        WHERE info_hash = ?
    )";
    
    sqlSelectTorrent_ = R"(
        SELECT info_hash, name, magnet_uri, size, date_added, last_active,
               save_path, progress, status, metadata, files, seeders, leechers,
               downloaded, uploaded, ratio
        FROM torrents WHERE info_hash = ?
    )";
    
    sqlDeleteTorrent_ = "DELETE FROM torrents WHERE info_hash = ?";
    
    // Media SQL statements
    sqlInsertMedia_ = R"(
        INSERT INTO media (id, torrent_hash, file_path, original_name, mime_type,
                         file_size, duration, width, height, frame_rate, video_codec,
                         audio_codec, has_transcription, date_added, last_played,
                         playback_position, metadata)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlUpdateMedia_ = R"(
        UPDATE media SET torrent_hash = ?, file_path = ?, original_name = ?,
                       mime_type = ?, file_size = ?, duration = ?, width = ?,
                       height = ?, frame_rate = ?, video_codec = ?, audio_codec = ?,
                       has_transcription = ?, last_played = ?, playback_position = ?,
                       metadata = ?
        WHERE id = ?
    )";
    
    sqlSelectMedia_ = R"(
        SELECT id, torrent_hash, file_path, original_name, mime_type, file_size,
               duration, width, height, frame_rate, video_codec, audio_codec,
               has_transcription, date_added, last_played, playback_position, metadata
        FROM media WHERE id = ?
    )";
    
    sqlDeleteMedia_ = "DELETE FROM media WHERE id = ?";
    
    // Transcription SQL statements
    sqlInsertTranscription_ = R"(
        INSERT INTO transcriptions (id, media_id, language, model_used, full_text,
                                  timestamps, confidence, date_created, processing_time, status)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlUpdateTranscription_ = R"(
        UPDATE transcriptions SET language = ?, model_used = ?, full_text = ?,
                                timestamps = ?, confidence = ?, processing_time = ?, status = ?
        WHERE id = ?
    )";
    
    sqlSelectTranscription_ = R"(
        SELECT id, media_id, language, model_used, full_text, timestamps,
               confidence, date_created, processing_time, status
        FROM transcriptions WHERE id = ?
    )";
    
    sqlDeleteTranscription_ = "DELETE FROM transcriptions WHERE id = ?";
    
    // Playback session SQL statements
    sqlInsertSession_ = R"(
        INSERT INTO playback_sessions (session_id, media_id, start_time, end_time,
                                     start_position, end_position, total_duration, completed)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlUpdateSession_ = R"(
        UPDATE playback_sessions SET end_time = ?, end_position = ?, 
                                   total_duration = ?, completed = ?
        WHERE session_id = ?
    )";
    
    sqlSelectSession_ = R"(
        SELECT session_id, media_id, start_time, end_time, start_position,
               end_position, total_duration, completed
        FROM playback_sessions WHERE session_id = ?
    )";
}

StorageManager::~StorageManager() {
    close();
}

Expected<bool, StorageError> StorageManager::initialize(const QString& databasePath) {
    QMutexLocker locker(&databaseMutex_);
    
    QString dbPath = databasePath;
    if (dbPath.isEmpty()) {
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        dbPath = QDir(dataDir).filePath("murmur.db");
    }
    
    // Create database connection
    database_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    database_.setDatabaseName(dbPath);
    
    if (!database_.open()) {
        Logger::instance().error("Failed to open database: {}", database_.lastError().text().toStdString());
        return makeUnexpected(StorageError::ConnectionFailed);
    }
    
    // Configure database
    QSqlQuery config(database_);
    
    // Set cache size
    config.exec(QString("PRAGMA cache_size = -%1").arg(DEFAULT_CACHE_SIZE_MB * 1024));
    
    // Set journal mode
    config.exec("PRAGMA journal_mode = " + DEFAULT_JOURNAL_MODE);
    
    // Enable foreign keys
    config.exec("PRAGMA foreign_keys = ON");
    
    // Set synchronous mode for better performance
    config.exec("PRAGMA synchronous = NORMAL");
    
    // Create tables if they don't exist
    auto createResult = createTables();
    if (createResult.hasError()) {
        return createResult;
    }
    
    // Validate schema
    auto validateResult = validateSchema();
    if (validateResult.hasError()) {
        return validateResult;
    }
    
    Logger::instance().info("Database initialized successfully: {}", dbPath.toStdString());
    return true;
}

void StorageManager::close() {
    QMutexLocker locker(&databaseMutex_);
    
    if (database_.isOpen()) {
        if (inTransaction_) {
            database_.rollback();
        }
        database_.close();
    }
    
    // Release reference to the connection
    database_ = QSqlDatabase(); 
    QSqlDatabase::removeDatabase(connectionName_);
}

bool StorageManager::isOpen() const {
    QMutexLocker locker(&databaseMutex_);
    return database_.isOpen();
}

Expected<bool, StorageError> StorageManager::beginTransaction() {
    QMutexLocker locker(&databaseMutex_);
    
    if (!database_.isOpen()) {
        return makeUnexpected(StorageError::DatabaseNotOpen);
    }
    
    if (inTransaction_) {
        return true;  // Already in transaction
    }
    
    if (!database_.transaction()) {
        Logger::instance().error("Failed to begin transaction: {}", database_.lastError().text().toStdString());
        return makeUnexpected(StorageError::QueryFailed);
    }
    
    inTransaction_ = true;
    return true;
}

Expected<bool, StorageError> StorageManager::commitTransaction() {
    QMutexLocker locker(&databaseMutex_);
    
    if (!inTransaction_) {
        return true;  // No transaction to commit
    }
    
    if (!database_.commit()) {
        Logger::instance().error("Failed to commit transaction: {}", database_.lastError().text().toStdString());
        return makeUnexpected(StorageError::QueryFailed);
    }
    
    inTransaction_ = false;
    return true;
}

Expected<bool, StorageError> StorageManager::rollbackTransaction() {
    QMutexLocker locker(&databaseMutex_);
    
    if (!inTransaction_) {
        return true;  // No transaction to rollback
    }
    
    if (!database_.rollback()) {
        Logger::instance().error("Failed to rollback transaction: {}", database_.lastError().text().toStdString());
        return makeUnexpected(StorageError::QueryFailed);
    }
    
    inTransaction_ = false;
    return true;
}

Expected<bool, StorageError> StorageManager::addTorrent(const TorrentRecord& torrent) {
    auto validateResult = validateTorrentRecord(torrent);
    if (validateResult.hasError()) {
        return validateResult;
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlInsertTorrent_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    bindTorrentParams(query, torrent);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return executeResult;
    }
    
    emit torrentAdded(torrent.infoHash);
    return true;
}

Expected<bool, StorageError> StorageManager::updateTorrent(const TorrentRecord& torrent) {
    auto validateResult = validateTorrentRecord(torrent);
    if (validateResult.hasError()) {
        return validateResult;
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlUpdateTorrent_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    
    // Bind parameters (excluding info_hash which is WHERE condition)
    query.bindValue(0, torrent.name);
    query.bindValue(1, torrent.magnetUri);
    query.bindValue(2, torrent.size);
    query.bindValue(3, torrent.lastActive);
    query.bindValue(4, torrent.savePath);
    query.bindValue(5, torrent.progress);
    query.bindValue(6, torrent.status);
    query.bindValue(7, QJsonDocument(torrent.metadata).toJson(QJsonDocument::Compact));
    query.bindValue(8, torrent.files.join(";"));
    query.bindValue(9, torrent.seeders);
    query.bindValue(10, torrent.leechers);
    query.bindValue(11, torrent.downloaded);
    query.bindValue(12, torrent.uploaded);
    query.bindValue(13, torrent.ratio);
    query.bindValue(14, torrent.infoHash);  // WHERE condition
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return executeResult;
    }
    
    emit torrentUpdated(torrent.infoHash);
    return true;
}

Expected<TorrentRecord, StorageError> StorageManager::getTorrent(const QString& infoHash) {
    if (!InputValidator::validateInfoHash(infoHash)) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlSelectTorrent_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, infoHash);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    if (!query.next()) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    return torrentFromQuery(query);
}

Expected<QList<TorrentRecord>, StorageError> StorageManager::getAllTorrents() {
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("SELECT * FROM torrents ORDER BY date_added DESC");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    QList<TorrentRecord> torrents;
    while (query.next()) {
        torrents.append(torrentFromQuery(query));
    }
    
    return torrents;
}

Expected<bool, StorageError> StorageManager::createTables() {
    QStringList createStatements = {
        // Torrents table with strict constraints
        R"(CREATE TABLE IF NOT EXISTS torrents (
            info_hash TEXT PRIMARY KEY CHECK(length(info_hash) = 40 AND info_hash GLOB '[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]'),
            name TEXT NOT NULL CHECK(length(trim(name)) > 0),
            magnet_uri TEXT NOT NULL CHECK(magnet_uri LIKE 'magnet:?xt=urn:btih:%'),
            size INTEGER NOT NULL DEFAULT 0 CHECK(size >= 0),
            date_added TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            last_active TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            save_path TEXT NOT NULL DEFAULT '',
            progress REAL NOT NULL DEFAULT 0.0 CHECK(progress >= 0.0 AND progress <= 1.0),
            status TEXT NOT NULL DEFAULT 'inactive' CHECK(status IN ('inactive', 'downloading', 'seeding', 'paused', 'error', 'completed')),
            metadata TEXT NOT NULL DEFAULT '{}',
            files TEXT NOT NULL DEFAULT '',
            seeders INTEGER NOT NULL DEFAULT 0 CHECK(seeders >= 0),
            leechers INTEGER NOT NULL DEFAULT 0 CHECK(leechers >= 0),
            downloaded INTEGER NOT NULL DEFAULT 0 CHECK(downloaded >= 0),
            uploaded INTEGER NOT NULL DEFAULT 0 CHECK(uploaded >= 0),
            ratio REAL NOT NULL DEFAULT 0.0 CHECK(ratio >= 0.0)
        ))"
        
        // Media table with strict constraints
        R"(CREATE TABLE IF NOT EXISTS media (
            id TEXT PRIMARY KEY CHECK(length(trim(id)) > 0),
            torrent_hash TEXT CHECK(torrent_hash IS NULL OR (length(torrent_hash) = 40 AND torrent_hash GLOB '[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]')),
            file_path TEXT NOT NULL CHECK(length(trim(file_path)) > 0),
            original_name TEXT NOT NULL CHECK(length(trim(original_name)) > 0),
            mime_type TEXT NOT NULL DEFAULT '',
            file_size INTEGER NOT NULL DEFAULT 0 CHECK(file_size >= 0),
            duration INTEGER NOT NULL DEFAULT 0 CHECK(duration >= 0),
            width INTEGER NOT NULL DEFAULT 0 CHECK(width >= 0),
            height INTEGER NOT NULL DEFAULT 0 CHECK(height >= 0),
            frame_rate REAL NOT NULL DEFAULT 0.0 CHECK(frame_rate >= 0.0),
            video_codec TEXT NOT NULL DEFAULT '',
            audio_codec TEXT NOT NULL DEFAULT '',
            has_transcription BOOLEAN NOT NULL DEFAULT FALSE,
            date_added TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            last_played TIMESTAMP NULL,
            playback_position INTEGER NOT NULL DEFAULT 0 CHECK(playback_position >= 0),
            metadata TEXT NOT NULL DEFAULT '{}',
            FOREIGN KEY (torrent_hash) REFERENCES torrents(info_hash) ON DELETE CASCADE
        ))",
        
        // Transcriptions table
        R"(CREATE TABLE IF NOT EXISTS transcriptions (
            id TEXT PRIMARY KEY,
            media_id TEXT NOT NULL,
            language TEXT DEFAULT 'auto',
            model_used TEXT,
            full_text TEXT,
            timestamps TEXT,
            confidence REAL DEFAULT 0.0,
            date_created TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            processing_time INTEGER DEFAULT 0,
            status TEXT DEFAULT 'pending',
            FOREIGN KEY (media_id) REFERENCES media(id) ON DELETE CASCADE
        ))",
        
        // Playback sessions table
        R"(CREATE TABLE IF NOT EXISTS playback_sessions (
            session_id TEXT PRIMARY KEY,
            media_id TEXT NOT NULL,
            start_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            end_time TIMESTAMP,
            start_position INTEGER DEFAULT 0,
            end_position INTEGER DEFAULT 0,
            total_duration INTEGER DEFAULT 0,
            completed BOOLEAN DEFAULT FALSE,
            FOREIGN KEY (media_id) REFERENCES media(id) ON DELETE CASCADE
        ))",
        
        // Indexes
        "CREATE INDEX IF NOT EXISTS idx_torrents_status ON torrents(status)",
        "CREATE INDEX IF NOT EXISTS idx_torrents_date_added ON torrents(date_added)",
        "CREATE INDEX IF NOT EXISTS idx_media_torrent_hash ON media(torrent_hash)",
        "CREATE INDEX IF NOT EXISTS idx_media_date_added ON media(date_added)",
        "CREATE INDEX IF NOT EXISTS idx_transcriptions_media_id ON transcriptions(media_id)",
        "CREATE INDEX IF NOT EXISTS idx_playback_sessions_media_id ON playback_sessions(media_id)"
    };
    
    for (const QString& statement : createStatements) {
        QSqlQuery query(database_);
        if (!query.exec(statement)) {
            Logger::instance().error("Failed to create table: {}", query.lastError().text().toStdString());
            return makeUnexpected(StorageError::QueryFailed);
        }
    }
    
    return true;
}

Expected<bool, StorageError> StorageManager::validateSchema() {
    // Check if required tables exist
    QStringList requiredTables = {"torrents", "media", "transcriptions", "playback_sessions"};
    
    QSqlQuery query(database_);
    query.exec("SELECT name FROM sqlite_master WHERE type='table'");
    
    QStringList existingTables;
    while (query.next()) {
        existingTables.append(query.value(0).toString());
    }
    
    for (const QString& tableName : requiredTables) {
        if (!existingTables.contains(tableName)) {
            Logger::instance().error("Required table missing: {}", tableName.toStdString());
            return makeUnexpected(StorageError::QueryFailed);
        }
    }
    
    return true;
}

Expected<QSqlQuery, StorageError> StorageManager::prepareQuery(const QString& sql) {
    if (!database_.isOpen()) {
        return makeUnexpected(StorageError::DatabaseNotOpen);
    }
    
    QSqlQuery query(database_);
    if (!query.prepare(sql)) {
        Logger::instance().error("Failed to prepare query: {}", query.lastError().text().toStdString());
        return makeUnexpected(StorageError::QueryFailed);
    }
    
    return query;
}

Expected<bool, StorageError> StorageManager::executeQuery(QSqlQuery& query) {
    if (!query.exec()) {
        Logger::instance().error("Query execution failed: {}", query.lastError().text().toStdString());
        return makeUnexpected(mapSqlError(query.lastError()));
    }
    
    return true;
}

TorrentRecord StorageManager::torrentFromQuery(const QSqlQuery& query) {
    TorrentRecord torrent;
    
    torrent.infoHash = query.value("info_hash").toString();
    torrent.name = query.value("name").toString();
    torrent.magnetUri = query.value("magnet_uri").toString();
    torrent.size = query.value("size").toLongLong();
    torrent.dateAdded = query.value("date_added").toDateTime();
    torrent.lastActive = query.value("last_active").toDateTime();
    torrent.savePath = query.value("save_path").toString();
    torrent.progress = query.value("progress").toDouble();
    torrent.status = query.value("status").toString();
    
    // Parse JSON metadata
    QString metadataJson = query.value("metadata").toString();
    if (!metadataJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(metadataJson.toUtf8());
        torrent.metadata = doc.object();
    }
    
    // Parse file list
    QString filesString = query.value("files").toString();
    if (!filesString.isEmpty()) {
        torrent.files = filesString.split(";", Qt::SkipEmptyParts);
    }
    
    torrent.seeders = query.value("seeders").toInt();
    torrent.leechers = query.value("leechers").toInt();
    torrent.downloaded = query.value("downloaded").toLongLong();
    torrent.uploaded = query.value("uploaded").toLongLong();
    torrent.ratio = query.value("ratio").toDouble();
    
    return torrent;
}

void StorageManager::bindTorrentParams(QSqlQuery& query, const TorrentRecord& torrent) {
    query.bindValue(0, torrent.infoHash);
    query.bindValue(1, torrent.name);
    query.bindValue(2, torrent.magnetUri);
    query.bindValue(3, torrent.size);
    query.bindValue(4, torrent.dateAdded);
    query.bindValue(5, torrent.lastActive);
    query.bindValue(6, torrent.savePath);
    query.bindValue(7, torrent.progress);
    query.bindValue(8, torrent.status);
    query.bindValue(9, QJsonDocument(torrent.metadata).toJson(QJsonDocument::Compact));
    query.bindValue(10, torrent.files.join(";"));
    query.bindValue(11, torrent.seeders);
    query.bindValue(12, torrent.leechers);
    query.bindValue(13, torrent.downloaded);
    query.bindValue(14, torrent.uploaded);
    query.bindValue(15, torrent.ratio);
}

Expected<bool, StorageError> StorageManager::validateTorrentRecord(const TorrentRecord& torrent) {
    if (torrent.infoHash.isEmpty() || !InputValidator::validateInfoHash(torrent.infoHash)) {
        Logger::instance().error("Invalid info hash: '{}'", torrent.infoHash.toStdString());
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (torrent.name.isEmpty() || torrent.name.length() > 255) {
        Logger::instance().error("Invalid name: '{}' (length: {})", torrent.name.toStdString(), torrent.name.length());
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (torrent.magnetUri.isEmpty() || !InputValidator::validateMagnetUri(torrent.magnetUri)) {
        Logger::instance().error("Invalid magnet URI: '{}'", torrent.magnetUri.toStdString());
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (torrent.size < 0) {
        Logger::instance().error("Invalid size: {}", torrent.size);
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (torrent.progress < 0.0 || torrent.progress > 1.0) {
        Logger::instance().error("Invalid progress: {}", torrent.progress);
        return makeUnexpected(StorageError::InvalidData);
    }
    
    return true;
}

QString StorageManager::generateId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

StorageError StorageManager::mapSqlError(const QSqlError& error) {
    // First check error text for specific constraint violations
    QString errorText = error.text().toLower();
    
    if (errorText.contains("unique constraint") || 
        errorText.contains("primary key constraint") ||
        errorText.contains("foreign key constraint") ||
        errorText.contains("check constraint") ||
        errorText.contains("not null constraint")) {
        return StorageError::ConstraintViolation;
    }
    
    // Then check by error type
    switch (error.type()) {
        case QSqlError::ConnectionError:
            return StorageError::ConnectionFailed;
        case QSqlError::StatementError:
        case QSqlError::TransactionError:
            return StorageError::QueryFailed;
        case QSqlError::UnknownError:
        default:
            return StorageError::QueryFailed;
    }
}

// Placeholder implementations for remaining methods
Expected<bool, StorageError> StorageManager::migrateDatabase() {
    QMutexLocker locker(&databaseMutex_);
    
    // Get current schema version
    int currentVersion = 0;
    {
        QSqlQuery versionQuery(database_);
        versionQuery.prepare("PRAGMA user_version");
        if (!versionQuery.exec() || !versionQuery.next()) {
            return makeUnexpected(StorageError::QueryFailed);
        }
        currentVersion = versionQuery.value(0).toInt();
    }
    
    // If we're already at the latest version, no migration needed
    if (currentVersion >= CURRENT_SCHEMA_VERSION) {
        return true;
    }
    
    Logger::instance().info("Migrating database from version {} to {}", currentVersion, CURRENT_SCHEMA_VERSION);
    
    // Begin transaction
    if (!database_.transaction()) {
        return makeUnexpected(StorageError::TransactionFailed);
    }
    
    // Apply migrations step by step
    for (int version = currentVersion + 1; version <= CURRENT_SCHEMA_VERSION; ++version) {
        if (!applyMigration(version)) {
            database_.rollback();
            return makeUnexpected(StorageError::MigrationFailed);
        }
    }
    
    // Update schema version
    QSqlQuery updateVersion(database_);
    updateVersion.prepare(QString("PRAGMA user_version = %1").arg(CURRENT_SCHEMA_VERSION));
    if (!updateVersion.exec()) {
        database_.rollback();
        return makeUnexpected(StorageError::QueryFailed);
    }
    
    // Commit transaction
    if (!database_.commit()) {
        return makeUnexpected(StorageError::TransactionFailed);
    }
    
    Logger::instance().info("Database migration completed successfully");
    return true;
}

bool StorageManager::applyMigration(int toVersion) {
    QSqlQuery query(database_);
    
    switch (toVersion) {
        case 1:
            // Initial schema creation
            if (!query.exec(R"(
                CREATE TABLE IF NOT EXISTS media (
                    media_id TEXT PRIMARY KEY,
                    torrent_hash TEXT NOT NULL,
                    filename TEXT NOT NULL,
                    file_size INTEGER NOT NULL,
                    mime_type TEXT,
                    duration_seconds REAL,
                    date_added INTEGER NOT NULL,
                    last_accessed INTEGER,
                    playback_position REAL DEFAULT 0.0,
                    is_favorite BOOLEAN DEFAULT 0,
                    tags TEXT,
                    metadata TEXT
                )
            )")) {
                return false;
            }
            
            if (!query.exec(R"(
                CREATE TABLE IF NOT EXISTS transcriptions (
                    transcription_id TEXT PRIMARY KEY,
                    media_id TEXT NOT NULL,
                    language TEXT NOT NULL,
                    full_text TEXT NOT NULL,
                    segments TEXT NOT NULL,
                    confidence REAL,
                    date_created INTEGER NOT NULL,
                    model_version TEXT,
                    FOREIGN KEY (media_id) REFERENCES media (media_id) ON DELETE CASCADE
                )
            )")) {
                return false;
            }
            
            if (!query.exec(R"(
                CREATE TABLE IF NOT EXISTS sessions (
                    session_id TEXT PRIMARY KEY,
                    session_type TEXT NOT NULL,
                    start_time INTEGER NOT NULL,
                    end_time INTEGER,
                    media_ids TEXT,
                    settings TEXT,
                    status TEXT DEFAULT 'active'
                )
            )")) {
                return false;
            }
            
            // Create indexes
            if (!query.exec("CREATE INDEX IF NOT EXISTS idx_media_torrent_hash ON media(torrent_hash)")) {
                return false;
            }
            
            if (!query.exec("CREATE INDEX IF NOT EXISTS idx_media_date_added ON media(date_added)")) {
                return false;
            }
            
            if (!query.exec("CREATE INDEX IF NOT EXISTS idx_transcriptions_media_id ON transcriptions(media_id)")) {
                return false;
            }
            
            if (!query.exec("CREATE INDEX IF NOT EXISTS idx_sessions_start_time ON sessions(start_time)")) {
                return false;
            }
            
            break;
            
        default:
            Logger::instance().warn("Unknown migration version: {}", toVersion);
            return false;
    }
    
    return true;
}

Expected<MediaRecord, StorageError> StorageManager::getMedia(const QString& mediaId) {
    // Media IDs are UUIDs, not torrent info hashes - validate as UUID format
    if (mediaId.isEmpty() || mediaId.length() > 255) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlSelectMedia_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, mediaId);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    if (!query.next()) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    return mediaFromQuery(query);
}

Expected<QList<MediaRecord>, StorageError> StorageManager::getMediaByTorrent(const QString& torrentHash) {
    if (!InputValidator::validateInfoHash(torrentHash)) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("SELECT * FROM media WHERE torrent_hash = ? ORDER BY date_added DESC");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, torrentHash);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    QList<MediaRecord> mediaList;
    while (query.next()) {
        mediaList.append(mediaFromQuery(query));
    }
    
    return mediaList;
}

Expected<QList<MediaRecord>, StorageError> StorageManager::searchMedia(const QString& query) {
    QMutexLocker locker(&databaseMutex_);
    
    QString searchQuery = "SELECT * FROM media WHERE original_name LIKE ? OR file_path LIKE ? ORDER BY date_added DESC";
    auto queryResult = prepareQuery(searchQuery);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery sqlQuery = std::move(queryResult.value());
    QString searchPattern = "%" + query + "%";
    sqlQuery.bindValue(0, searchPattern);
    sqlQuery.bindValue(1, searchPattern);
    
    auto executeResult = executeQuery(sqlQuery);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    QList<MediaRecord> mediaList;
    while (sqlQuery.next()) {
        mediaList.append(mediaFromQuery(sqlQuery));
    }
    
    return mediaList;
}

Expected<bool, StorageError> StorageManager::updatePlaybackPosition(const QString& mediaId, qint64 position) {
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("UPDATE media SET playback_position = ?, last_played = ? WHERE id = ?");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, position);
    query.bindValue(1, QDateTime::currentDateTime());
    query.bindValue(2, mediaId);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return executeResult;
    }
    
    return true;
}

Expected<QString, StorageError> StorageManager::addMedia(const MediaRecord& media) {    
    // Create a copy to modify the ID if needed
    MediaRecord mediaWithId = media;
    if (mediaWithId.id.isEmpty()) {
        mediaWithId.id = generateId();
    }
 
    // First check the record's built-in validation
    if (!mediaWithId.isValid()) {
        Logger::instance().error("MediaRecord failed built-in validation");
        return makeUnexpected(StorageError::InvalidData);
    }
    
    // Then perform additional validation
    auto validateResult = validateMediaRecord(mediaWithId);
    if (validateResult.hasError()) {
        return makeUnexpected(validateResult.error());
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlInsertMedia_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    bindMediaParams(query, mediaWithId);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    emit mediaAdded(mediaWithId.id);
    return mediaWithId.id;
}

MediaRecord StorageManager::mediaFromQuery(const QSqlQuery& query) {
    MediaRecord media;
    
    media.id = query.value("id").toString();
    media.torrentHash = query.value("torrent_hash").toString();
    media.filePath = query.value("file_path").toString();
    media.originalName = query.value("original_name").toString();
    media.mimeType = query.value("mime_type").toString();
    media.fileSize = query.value("file_size").toLongLong();
    media.duration = query.value("duration").toLongLong();
    media.width = query.value("width").toInt();
    media.height = query.value("height").toInt();
    media.frameRate = query.value("frame_rate").toDouble();
    media.videoCodec = query.value("video_codec").toString();
    media.audioCodec = query.value("audio_codec").toString();
    media.hasTranscription = query.value("has_transcription").toBool();
    media.dateAdded = query.value("date_added").toDateTime();
    media.lastPlayed = query.value("last_played").toDateTime();
    media.playbackPosition = query.value("playback_position").toLongLong();
    
    // Parse JSON metadata
    QString metadataJson = query.value("metadata").toString();
    if (!metadataJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(metadataJson.toUtf8());
        media.metadata = doc.object();
    }
    
    return media;
}

void StorageManager::bindMediaParams(QSqlQuery& query, const MediaRecord& media) {
    query.bindValue(0, media.id);
    query.bindValue(1, media.torrentHash);
    query.bindValue(2, media.filePath);
    query.bindValue(3, media.originalName);
    query.bindValue(4, media.mimeType);
    query.bindValue(5, media.fileSize);
    query.bindValue(6, media.duration);
    query.bindValue(7, media.width);
    query.bindValue(8, media.height);
    query.bindValue(9, media.frameRate);
    query.bindValue(10, media.videoCodec);
    query.bindValue(11, media.audioCodec);
    query.bindValue(12, media.hasTranscription);
    query.bindValue(13, media.dateAdded);
    query.bindValue(14, media.lastPlayed);
    query.bindValue(15, media.playbackPosition);
    query.bindValue(16, QJsonDocument(media.metadata).toJson(QJsonDocument::Compact));
}

void StorageManager::bindSessionParams(QSqlQuery& query, const PlaybackSession& session) {
    query.bindValue(0, session.sessionId);
    query.bindValue(1, session.mediaId);
    query.bindValue(2, session.startTime);
    query.bindValue(3, session.endTime);
    query.bindValue(4, session.startPosition);
    query.bindValue(5, session.endPosition);
    query.bindValue(6, session.totalDuration);
    query.bindValue(7, session.completed);
}

Expected<bool, StorageError> StorageManager::validateMediaRecord(const MediaRecord& media) {
    if (media.id.isEmpty() || media.id.length() > 255) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (media.filePath.isEmpty() || !InputValidator::validateFilePath(media.filePath)) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (media.originalName.isEmpty() || !InputValidator::validateFileName(media.originalName)) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (media.fileSize < 0) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (media.duration < 0) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (media.width < 0 || media.height < 0) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (media.frameRate < 0.0) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    return true;
}

Expected<QString, StorageError> StorageManager::addTranscription(const TranscriptionRecord& transcription) {
    // Create a copy to modify the ID if needed
    TranscriptionRecord transcriptionWithId = transcription;
    if (transcriptionWithId.id.isEmpty()) {
        transcriptionWithId.id = generateId();
    }

    auto validateResult = validateTranscriptionRecord(transcription);
    if (validateResult.hasError()) {
        return makeUnexpected(validateResult.error());
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlInsertTranscription_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    bindTranscriptionParams(query, transcriptionWithId);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    return transcriptionWithId.id;
}

Expected<bool, StorageError> StorageManager::updateMedia(const MediaRecord& media) {
    auto validateResult = validateMediaRecord(media);
    if (validateResult.hasError()) {
        return validateResult;
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlUpdateMedia_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    
    // Bind parameters (excluding id which is WHERE condition)
    query.bindValue(0, media.torrentHash);
    query.bindValue(1, media.filePath);
    query.bindValue(2, media.originalName);
    query.bindValue(3, media.mimeType);
    query.bindValue(4, media.fileSize);
    query.bindValue(5, media.duration);
    query.bindValue(6, media.width);
    query.bindValue(7, media.height);
    query.bindValue(8, media.frameRate);
    query.bindValue(9, media.videoCodec);
    query.bindValue(10, media.audioCodec);
    query.bindValue(11, media.hasTranscription);
    query.bindValue(12, media.lastPlayed);
    query.bindValue(13, media.playbackPosition);
    query.bindValue(14, QJsonDocument(media.metadata).toJson(QJsonDocument::Compact));
    query.bindValue(15, media.id);  // WHERE condition
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return executeResult;
    }
    
    emit mediaUpdated(media.id);
    return true;
}

Expected<TranscriptionRecord, StorageError> StorageManager::getTranscriptionByMedia(const QString& mediaId) {
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("SELECT * FROM transcriptions WHERE media_id = ? ORDER BY date_created DESC LIMIT 1");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, mediaId);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    if (!query.next()) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    return transcriptionFromQuery(query);
}

TranscriptionRecord StorageManager::transcriptionFromQuery(const QSqlQuery& query) {
    TranscriptionRecord transcription;
    
    transcription.id = query.value("id").toString();
    transcription.mediaId = query.value("media_id").toString();
    transcription.language = query.value("language").toString();
    transcription.modelUsed = query.value("model_used").toString();
    transcription.fullText = query.value("full_text").toString();
    transcription.confidence = query.value("confidence").toDouble();
    transcription.dateCreated = query.value("date_created").toDateTime();
    transcription.processingTime = query.value("processing_time").toLongLong();
    transcription.status = query.value("status").toString();
    
    // Parse JSON timestamps
    QString timestampsJson = query.value("timestamps").toString();
    if (!timestampsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(timestampsJson.toUtf8());
        transcription.timestamps = doc.object();
    }
    
    return transcription;
}

void StorageManager::bindTranscriptionParams(QSqlQuery& query, const TranscriptionRecord& transcription) {
    query.bindValue(0, transcription.id);
    query.bindValue(1, transcription.mediaId);
    query.bindValue(2, transcription.language);
    query.bindValue(3, transcription.modelUsed);
    query.bindValue(4, transcription.fullText);
    query.bindValue(5, QJsonDocument(transcription.timestamps).toJson(QJsonDocument::Compact));
    query.bindValue(6, transcription.confidence);
    query.bindValue(7, transcription.dateCreated);
    query.bindValue(8, transcription.processingTime);
    query.bindValue(9, transcription.status);
}

Expected<bool, StorageError> StorageManager::validateTranscriptionRecord(const TranscriptionRecord& transcription) {
    if (transcription.id.isEmpty() || transcription.id.length() > 255) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (transcription.mediaId.isEmpty() || transcription.mediaId.length() > 255) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (transcription.language.isEmpty() || transcription.language.length() > 10) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (transcription.confidence < 0.0 || transcription.confidence > 1.0) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (transcription.processingTime < 0) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    return true;
}

void StorageManager::setAutoCommit(bool autoCommit) {
    autoCommit_ = autoCommit;
}

void StorageManager::setCacheSize(int sizeMB) {
    if (database_.isOpen()) {
        QSqlQuery query(database_);
        query.exec(QString("PRAGMA cache_size = -%1").arg(sizeMB * 1024));
    }
}

void StorageManager::setJournalMode(const QString& mode) {
    if (database_.isOpen()) {
        QSqlQuery query(database_);
        query.exec("PRAGMA journal_mode = " + mode);
    }
}

void StorageManager::onDatabaseError() {
    Logger::instance().error("Database error occurred");
}

Expected<bool, StorageError> StorageManager::removeTorrent(const QString& infoHash) {
    if (!InputValidator::validateInfoHash(infoHash)) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlDeleteTorrent_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, infoHash);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return executeResult;
    }
    
    if (query.numRowsAffected() == 0) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    emit torrentRemoved(infoHash);
    Logger::instance().info("Removed torrent: {}", infoHash.toStdString());
    return true;
}

Expected<QList<TorrentRecord>, StorageError> StorageManager::getActiveTorrents() {
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("SELECT * FROM torrents WHERE status IN ('downloading', 'seeding', 'checking') ORDER BY last_active DESC");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    QList<TorrentRecord> torrents;
    while (query.next()) {
        torrents.append(torrentFromQuery(query));
    }
    
    return torrents;
}

Expected<bool, StorageError> StorageManager::updateTorrentProgress(const QString& infoHash, double progress) {
    if (!InputValidator::validateInfoHash(infoHash)) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (progress < 0.0 || progress > 1.0) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("UPDATE torrents SET progress = ?, last_active = ? WHERE info_hash = ?");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, progress);
    query.bindValue(1, QDateTime::currentDateTime());
    query.bindValue(2, infoHash);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return executeResult;
    }
    
    if (query.numRowsAffected() == 0) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    return true;
}

Expected<bool, StorageError> StorageManager::updateTorrentStatus(const QString& infoHash, const QString& status) {
    if (!InputValidator::validateInfoHash(infoHash)) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QStringList validStatuses = {"downloading", "seeding", "paused", "error", "completed", "checking"};
    if (!validStatuses.contains(status)) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("UPDATE torrents SET status = ?, last_active = ? WHERE info_hash = ?");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, status);
    query.bindValue(1, QDateTime::currentDateTime());
    query.bindValue(2, infoHash);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return executeResult;
    }
    
    if (query.numRowsAffected() == 0) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    return true;
}

Expected<bool, StorageError> StorageManager::removeMedia(const QString& mediaId) {
    if (mediaId.isEmpty()) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlDeleteMedia_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, mediaId);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return executeResult;
    }
    
    if (query.numRowsAffected() == 0) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    Logger::instance().info("Removed media: {}", mediaId.toStdString());
    return true;
}

Expected<QList<MediaRecord>, StorageError> StorageManager::getAllMedia() {
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("SELECT * FROM media ORDER BY date_added DESC");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    QList<MediaRecord> mediaList;
    while (query.next()) {
        mediaList.append(mediaFromQuery(query));
    }
    
    return mediaList;
}

Expected<QList<MediaRecord>, StorageError> StorageManager::getRecentMedia(int limit) {
    if (limit <= 0 || limit > 1000) {
        limit = 20; // Default fallback
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("SELECT * FROM media WHERE last_played IS NOT NULL ORDER BY last_played DESC LIMIT ?");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, limit);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    QList<MediaRecord> mediaList;
    while (query.next()) {
        mediaList.append(mediaFromQuery(query));
    }
    
    return mediaList;
}

Expected<bool, StorageError> StorageManager::updateTranscription(const TranscriptionRecord& transcription) {
    auto validateResult = validateTranscriptionRecord(transcription);
    if (validateResult.hasError()) {
        return validateResult;
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlUpdateTranscription_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    
    // Bind parameters (excluding id which is WHERE condition)
    query.bindValue(0, transcription.language);
    query.bindValue(1, transcription.modelUsed);
    query.bindValue(2, transcription.fullText);
    query.bindValue(3, QJsonDocument(transcription.timestamps).toJson(QJsonDocument::Compact));
    query.bindValue(4, transcription.confidence);
    query.bindValue(5, transcription.processingTime);
    query.bindValue(6, transcription.status);
    query.bindValue(7, transcription.id);  // WHERE condition
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return executeResult;
    }
    
    if (query.numRowsAffected() == 0) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    return true;
}

Expected<bool, StorageError> StorageManager::removeTranscription(const QString& transcriptionId) {
    if (transcriptionId.isEmpty()) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlDeleteTranscription_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, transcriptionId);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return executeResult;
    }
    
    if (query.numRowsAffected() == 0) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    Logger::instance().info("Removed transcription: {}", transcriptionId.toStdString());
    return true;
}

Expected<TranscriptionRecord, StorageError> StorageManager::getTranscription(const QString& transcriptionId) {
    if (transcriptionId.isEmpty()) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlSelectTranscription_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, transcriptionId);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    if (!query.next()) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    return transcriptionFromQuery(query);
}

Expected<QList<TranscriptionRecord>, StorageError> StorageManager::getAllTranscriptions() {
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("SELECT * FROM transcriptions ORDER BY date_created DESC");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    QList<TranscriptionRecord> transcriptions;
    while (query.next()) {
        transcriptions.append(transcriptionFromQuery(query));
    }
    
    return transcriptions;
}

Expected<bool, StorageError> StorageManager::updateTranscriptionStatus(const QString& transcriptionId, const QString& status) {
    if (transcriptionId.isEmpty()) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QStringList validStatuses = {"pending", "processing", "completed", "failed"};
    if (!validStatuses.contains(status)) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("UPDATE transcriptions SET status = ? WHERE id = ?");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, status);
    query.bindValue(1, transcriptionId);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return executeResult;
    }
    
    if (query.numRowsAffected() == 0) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    if (status == "completed") {
        // Update media has_transcription flag
        auto mediaUpdateResult = prepareQuery("UPDATE media SET has_transcription = TRUE WHERE id = (SELECT media_id FROM transcriptions WHERE id = ?)");
        if (mediaUpdateResult.hasValue()) {
            QSqlQuery mediaQuery = std::move(mediaUpdateResult.value());
            mediaQuery.bindValue(0, transcriptionId);
            executeQuery(mediaQuery);
        }
        
        emit transcriptionCompleted(transcriptionId);
    }
    
    return true;
}

// Complete missing playback session operations
Expected<QString, StorageError> StorageManager::recordPlaybackSession(const PlaybackSession& session) {
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlInsertSession_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    bindSessionParams(query, session);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    Logger::instance().info("Recorded playback session: {}", session.sessionId.toStdString());
    return session.sessionId;
}

Expected<bool, StorageError> StorageManager::updatePlaybackSession(const PlaybackSession& session) {
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery(sqlUpdateSession_);
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, session.endTime);
    query.bindValue(1, session.endPosition);
    query.bindValue(2, session.totalDuration);
    query.bindValue(3, session.completed);
    query.bindValue(4, session.sessionId);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return executeResult;
    }
    
    if (query.numRowsAffected() == 0) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    return true;
}

Expected<QList<PlaybackSession>, StorageError> StorageManager::getPlaybackHistory(const QString& mediaId, int limit) {
    if (mediaId.isEmpty()) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    if (limit <= 0 || limit > 1000) {
        limit = 10; // Default fallback
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("SELECT * FROM playback_sessions WHERE media_id = ? ORDER BY start_time DESC LIMIT ?");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, mediaId);
    query.bindValue(1, limit);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    QList<PlaybackSession> sessions;
    while (query.next()) {
        sessions.append(sessionFromQuery(query));
    }
    
    return sessions;
}

Expected<bool, StorageError> StorageManager::markSessionCompleted(const QString& sessionId) {
    if (sessionId.isEmpty()) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("UPDATE playback_sessions SET completed = TRUE, end_time = ? WHERE session_id = ?");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    query.bindValue(0, QDateTime::currentDateTime());
    query.bindValue(1, sessionId);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return executeResult;
    }
    
    if (query.numRowsAffected() == 0) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    return true;
}

Expected<bool, StorageError> StorageManager::clearPlaybackPositions() {
    QMutexLocker locker(&databaseMutex_);
    
    auto beginResult = beginTransaction();
    if (beginResult.hasError()) {
        return beginResult;
    }
    
    // Clear playback positions from media table
    auto clearMediaQueryResult = prepareQuery("UPDATE media SET playback_position = 0, last_played = NULL");
    if (clearMediaQueryResult.hasError()) {
        rollbackTransaction();
        return makeUnexpected(clearMediaQueryResult.error());
    }
    QSqlQuery clearMediaQuery = std::move(clearMediaQueryResult.value());
    auto clearMediaResult = executeQuery(clearMediaQuery);
    if (clearMediaResult.hasError()) {
        rollbackTransaction();
        return clearMediaResult;
    }
    
    // Clear playback sessions
    auto clearSessionsQueryResult = prepareQuery("DELETE FROM playback_sessions");
    if (clearSessionsQueryResult.hasError()) {
        rollbackTransaction();
        return makeUnexpected(clearSessionsQueryResult.error());
    }
    QSqlQuery clearSessionsQuery = std::move(clearSessionsQueryResult.value());
    auto clearSessionsResult = executeQuery(clearSessionsQuery);
    if (clearSessionsResult.hasError()) {
        rollbackTransaction();
        return clearSessionsResult;
    }
    
    auto commitResult = commitTransaction();
    if (commitResult.hasError()) {
        rollbackTransaction();
        return commitResult;
    }
    
    Logger::instance().info("Cleared all playback positions and history");
    return true;
}

// Complete missing search operations
Expected<QList<TorrentRecord>, StorageError> StorageManager::searchTorrents(const QString& searchQuery) {
    QString sanitizedQuery = sanitizeQuery(searchQuery);
    if (sanitizedQuery.isEmpty()) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("SELECT * FROM torrents WHERE name LIKE ? OR magnet_uri LIKE ? ORDER BY date_added DESC");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    QString searchPattern = "%" + sanitizedQuery + "%";
    query.bindValue(0, searchPattern);
    query.bindValue(1, searchPattern);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    QList<TorrentRecord> torrents;
    while (query.next()) {
        torrents.append(torrentFromQuery(query));
    }
    
    return torrents;
}

Expected<QList<TranscriptionRecord>, StorageError> StorageManager::searchTranscriptions(const QString& searchQuery) {
    QString sanitizedQuery = sanitizeQuery(searchQuery);
    if (sanitizedQuery.isEmpty()) {
        return makeUnexpected(StorageError::InvalidData);
    }
    
    QMutexLocker locker(&databaseMutex_);
    
    auto queryResult = prepareQuery("SELECT * FROM transcriptions WHERE full_text LIKE ? ORDER BY date_created DESC");
    if (queryResult.hasError()) {
        return makeUnexpected(queryResult.error());
    }
    
    QSqlQuery query = std::move(queryResult.value());
    QString searchPattern = "%" + sanitizedQuery + "%";
    query.bindValue(0, searchPattern);
    
    auto executeResult = executeQuery(query);
    if (executeResult.hasError()) {
        return makeUnexpected(executeResult.error());
    }
    
    QList<TranscriptionRecord> transcriptions;
    while (query.next()) {
        transcriptions.append(transcriptionFromQuery(query));
    }
    
    return transcriptions;
}

// Statistics operations 
Expected<QJsonObject, StorageError> StorageManager::getTorrentStatistics() {
    QMutexLocker locker(&databaseMutex_);
    
    QJsonObject stats;
    
    // Total torrents
    auto totalResult = executeScalar("SELECT COUNT(*) FROM torrents");
    if (totalResult.hasValue()) {
        stats["totalTorrents"] = totalResult.value().toInt();
    }
    
    // Active torrents
    auto activeResult = executeScalar("SELECT COUNT(*) FROM torrents WHERE status IN ('downloading', 'seeding')");
    if (activeResult.hasValue()) {
        stats["activeTorrents"] = activeResult.value().toInt();
    }
    
    // Completed torrents
    auto completedResult = executeScalar("SELECT COUNT(*) FROM torrents WHERE progress >= 1.0");
    if (completedResult.hasValue()) {
        stats["completedTorrents"] = completedResult.value().toInt();
    }
    
    // Total size
    auto sizeResult = executeScalar("SELECT SUM(size) FROM torrents");
    if (sizeResult.hasValue()) {
        stats["totalSizeBytes"] = static_cast<qint64>(sizeResult.value().toLongLong());
    }
    
    // Total downloaded
    auto downloadedResult = executeScalar("SELECT SUM(downloaded) FROM torrents");
    if (downloadedResult.hasValue()) {
        stats["totalDownloadedBytes"] = static_cast<qint64>(downloadedResult.value().toLongLong());
    }
    
    // Total uploaded
    auto uploadedResult = executeScalar("SELECT SUM(uploaded) FROM torrents");
    if (uploadedResult.hasValue()) {
        stats["totalUploadedBytes"] = static_cast<qint64>(uploadedResult.value().toLongLong());
    }
    
    // Average ratio
    auto ratioResult = executeScalar("SELECT AVG(ratio) FROM torrents WHERE ratio > 0");
    if (ratioResult.hasValue()) {
        stats["averageRatio"] = ratioResult.value().toDouble();
    }
    
    return stats;
}

Expected<QJsonObject, StorageError> StorageManager::getMediaStatistics() {
    QMutexLocker locker(&databaseMutex_);
    
    QJsonObject stats;
    
    // Total media files
    auto totalResult = executeScalar("SELECT COUNT(*) FROM media");
    if (totalResult.hasValue()) {
        stats["totalMediaFiles"] = totalResult.value().toInt();
    }
    
    // Files with transcriptions
    auto transcribedResult = executeScalar("SELECT COUNT(*) FROM media WHERE has_transcription = TRUE");
    if (transcribedResult.hasValue()) {
        stats["transcribedFiles"] = transcribedResult.value().toInt();
    }
    
    // Total duration
    auto durationResult = executeScalar("SELECT SUM(duration) FROM media WHERE duration > 0");
    if (durationResult.hasValue()) {
        stats["totalDurationMs"] = static_cast<qint64>(durationResult.value().toLongLong());
    }
    
    // Total file size
    auto sizeResult = executeScalar("SELECT SUM(file_size) FROM media");
    if (sizeResult.hasValue()) {
        stats["totalFileSizeBytes"] = static_cast<qint64>(sizeResult.value().toLongLong());
    }
    
    // Recent files (last 30 days)
    auto recentResult = executeScalar("SELECT COUNT(*) FROM media WHERE date_added > datetime('now', '-30 days')");
    if (recentResult.hasValue()) {
        stats["recentFiles"] = recentResult.value().toInt();
    }
    
    return stats;
}

Expected<QJsonObject, StorageError> StorageManager::getPlaybackStatistics() {
    QMutexLocker locker(&databaseMutex_);
    
    QJsonObject stats;
    
    // Total sessions
    auto totalResult = executeScalar("SELECT COUNT(*) FROM playback_sessions");
    if (totalResult.hasValue()) {
        stats["totalSessions"] = totalResult.value().toInt();
    }
    
    // Completed sessions
    auto completedResult = executeScalar("SELECT COUNT(*) FROM playback_sessions WHERE completed = TRUE");
    if (completedResult.hasValue()) {
        stats["completedSessions"] = completedResult.value().toInt();
    }
    
    // Total watch time
    auto watchTimeResult = executeScalar("SELECT SUM(end_position - start_position) FROM playback_sessions WHERE end_position > start_position");
    if (watchTimeResult.hasValue()) {
        stats["totalWatchTimeMs"] = static_cast<qint64>(watchTimeResult.value().toLongLong());
    }
    
    // Average session duration
    auto avgDurationResult = executeScalar("SELECT AVG(end_position - start_position) FROM playback_sessions WHERE end_position > start_position");
    if (avgDurationResult.hasValue()) {
        stats["averageSessionDurationMs"] = static_cast<qint64>(avgDurationResult.value().toLongLong());
    }
    
    // Recent sessions (last 7 days)
    auto recentResult = executeScalar("SELECT COUNT(*) FROM playback_sessions WHERE start_time > datetime('now', '-7 days')");
    if (recentResult.hasValue()) {
        stats["recentSessions"] = recentResult.value().toInt();
    }
    
    return stats;
}

Expected<qint64, StorageError> StorageManager::getTotalStorageUsed() {
    QMutexLocker locker(&databaseMutex_);
    
    // Get database file size
    if (!database_.isOpen()) {
        return makeUnexpected(StorageError::DatabaseNotOpen);
    }
    
    QString dbPath = database_.databaseName();
    QFileInfo dbInfo(dbPath);
    qint64 dbSize = dbInfo.size();
    
    // Get total media file sizes
    auto mediaSizeResult = executeScalar("SELECT SUM(file_size) FROM media");
    qint64 mediaSize = 0;
    if (mediaSizeResult.hasValue()) {
        mediaSize = mediaSizeResult.value().toLongLong();
    }
    
    return dbSize + mediaSize;
}

// Maintenance operations
Expected<bool, StorageError> StorageManager::vacuum() {
    QMutexLocker locker(&databaseMutex_);
    
    if (!database_.isOpen()) {
        return makeUnexpected(StorageError::DatabaseNotOpen);
    }
    
    QSqlQuery query(database_);
    if (!query.exec("VACUUM")) {
        Logger::instance().error("VACUUM failed: {}", query.lastError().text().toStdString());
        return makeUnexpected(StorageError::QueryFailed);
    }
    
    Logger::instance().info("Database VACUUM completed");
    return true;
}

Expected<bool, StorageError> StorageManager::reindex() {
    QMutexLocker locker(&databaseMutex_);
    
    if (!database_.isOpen()) {
        return makeUnexpected(StorageError::DatabaseNotOpen);
    }
    
    QSqlQuery query(database_);
    if (!query.exec("REINDEX")) {
        Logger::instance().error("REINDEX failed: {}", query.lastError().text().toStdString());
        return makeUnexpected(StorageError::QueryFailed);
    }
    
    Logger::instance().info("Database REINDEX completed");
    return true;
}

Expected<bool, StorageError> StorageManager::cleanupOrphanedRecords() {
    QMutexLocker locker(&databaseMutex_);
    
    if (!database_.isOpen()) {
        return makeUnexpected(StorageError::DatabaseNotOpen);
    }
    
    // Begin transaction for atomic cleanup
    auto transactionResult = beginTransaction();
    if (transactionResult.hasError()) {
        return transactionResult;
    }
    
    try {
        // Remove media records with invalid torrent references
        QSqlQuery cleanupMedia(database_);
        cleanupMedia.exec("DELETE FROM media WHERE torrent_hash IS NOT NULL AND torrent_hash NOT IN (SELECT info_hash FROM torrents)");
        
        // Remove transcriptions for non-existent media
        QSqlQuery cleanupTranscriptions(database_);
        cleanupTranscriptions.exec("DELETE FROM transcriptions WHERE media_id NOT IN (SELECT id FROM media)");
        
        // Remove playback sessions for non-existent media
        QSqlQuery cleanupSessions(database_);
        cleanupSessions.exec("DELETE FROM playback_sessions WHERE media_id NOT IN (SELECT id FROM media)");
        
        auto commitResult = commitTransaction();
        if (commitResult.hasError()) {
            rollbackTransaction();
            return commitResult;
        }
        
        Logger::instance().info("Orphaned records cleanup completed");
        return true;
        
    } catch (const std::exception& e) {
        rollbackTransaction();
        Logger::instance().error("Cleanup failed: {}", e.what());
        return makeUnexpected(StorageError::QueryFailed);
    }
}

Expected<bool, StorageError> StorageManager::backupDatabase(const QString& backupPath) {
    QMutexLocker locker(&databaseMutex_);
    
    if (!database_.isOpen()) {
        return makeUnexpected(StorageError::DatabaseNotOpen);
    }
    
    QString dbPath = database_.databaseName();
    QFileInfo dbInfo(dbPath);
    
    if (!dbInfo.exists()) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    // Ensure backup directory exists
    QFileInfo backupInfo(backupPath);
    QDir backupDir = backupInfo.dir();
    if (!backupDir.exists() && !backupDir.mkpath(backupDir.absolutePath())) {
        return makeUnexpected(StorageError::PermissionDenied);
    }
    
    // Remove existing backup file if it exists
    if (QFile::exists(backupPath)) {
        if (!QFile::remove(backupPath)) {
            Logger::instance().error("Failed to remove existing backup file: {}", backupPath.toStdString());
            return makeUnexpected(StorageError::PermissionDenied);
        }
    }
    
    // Ensure all pending transactions are committed before backup
    QSqlQuery commitQuery(database_);
    commitQuery.exec("PRAGMA wal_checkpoint(FULL)");
    
    // Copy database file
    if (!QFile::copy(dbPath, backupPath)) {
        Logger::instance().error("Failed to backup database to: {}", backupPath.toStdString());
        return makeUnexpected(StorageError::PermissionDenied);
    }
    
    // Verify backup file was created and is not empty
    QFileInfo backupFileInfo(backupPath);
    if (!backupFileInfo.exists() || backupFileInfo.size() == 0) {
        Logger::instance().error("Backup file is empty or was not created: {}", backupPath.toStdString());
        return makeUnexpected(StorageError::PermissionDenied);
    }
    
    Logger::instance().info("Database backed up to: {} ({} bytes)", backupPath.toStdString(), backupFileInfo.size());
    return true;
}

Expected<bool, StorageError> StorageManager::restoreDatabase(const QString& backupPath) {
    QMutexLocker locker(&databaseMutex_);
    
    QFileInfo backupInfo(backupPath);
    if (!backupInfo.exists()) {
        return makeUnexpected(StorageError::DataNotFound);
    }
    
    // Close current database
    QString currentPath = database_.databaseName();
    database_.close();
    
    // Remove current database
    if (QFile::exists(currentPath)) {
        if (!QFile::remove(currentPath)) {
            Logger::instance().error("Failed to remove current database for restore");
            return makeUnexpected(StorageError::PermissionDenied);
        }
    }
    
    // Copy backup to current location
    if (!QFile::copy(backupPath, currentPath)) {
        Logger::instance().error("Failed to restore database from: {}", backupPath.toStdString());
        return makeUnexpected(StorageError::PermissionDenied);
    }
    
    // Reopen database
    if (!database_.open()) {
        Logger::instance().error("Failed to reopen restored database");
        return makeUnexpected(StorageError::ConnectionFailed);
    }
    
    // Reinitialize database configuration
    QSqlQuery config(database_);
    
    // Set cache size
    config.exec(QString("PRAGMA cache_size = -%1").arg(DEFAULT_CACHE_SIZE_MB * 1024));
    
    // Set journal mode
    config.exec("PRAGMA journal_mode = " + DEFAULT_JOURNAL_MODE);
    
    // Enable foreign keys
    config.exec("PRAGMA foreign_keys = ON");
    
    // Validate that the restored database has the expected schema
    auto validateResult = validateSchema();
    if (validateResult.hasError()) {
        Logger::instance().warn("Restored database schema validation failed, attempting to recreate schema");
        auto schemaResult = createTables();
        if (schemaResult.hasError()) {
            Logger::instance().error("Failed to create schema for restored database");
            return schemaResult;
        }
    }
    
    Logger::instance().info("Database restored from: {}", backupPath.toStdString());
    return true;
}

Expected<bool, StorageError> StorageManager::testMigrateDatabase() {
    // For testing purposes, provide access to the private migration method
    return migrateDatabase();
}

// Helper implementations
Expected<QVariant, StorageError> StorageManager::executeScalar(const QString& sql, const QVariantList& params) const {
    if (!database_.isOpen()) {
        return makeUnexpected(StorageError::DatabaseNotOpen);
    }
    
    QSqlQuery query(database_);
    if (!query.prepare(sql)) {
        Logger::instance().error("Failed to prepare scalar query: {}", query.lastError().text().toStdString());
        return makeUnexpected(StorageError::QueryFailed);
    }
    
    // Bind parameters
    for (int i = 0; i < params.size(); ++i) {
        query.bindValue(i, params[i]);
    }
    
    if (!query.exec()) {
        Logger::instance().error("Scalar query execution failed: {}", query.lastError().text().toStdString());
        return makeUnexpected(StorageError::QueryFailed);
    }
    
    if (query.next()) {
        return query.value(0);
    }
    
    return QVariant(); // Return null variant if no result
}

PlaybackSession StorageManager::sessionFromQuery(const QSqlQuery& query) {
    PlaybackSession session;
    
    session.sessionId = query.value("session_id").toString();
    session.mediaId = query.value("media_id").toString();
    session.startTime = query.value("start_time").toDateTime();
    session.endTime = query.value("end_time").toDateTime();
    session.startPosition = query.value("start_position").toLongLong();
    session.endPosition = query.value("end_position").toLongLong();
    session.totalDuration = query.value("total_duration").toLongLong();
    session.completed = query.value("completed").toBool();
    
    return session;
}

QString StorageManager::sanitizeQuery(const QString& query) {
    QString sanitized = query;
    
    // Remove SQL injection attempts
    sanitized.remove(QRegularExpression(R"([';\"\\])")); // Remove quotes and backslashes
    sanitized.remove(QRegularExpression(R"(\b(DROP|DELETE|INSERT|UPDATE|CREATE|ALTER|EXEC|EXECUTE)\b)", QRegularExpression::CaseInsensitiveOption));
    
    // Limit length
    if (sanitized.length() > 255) {
        sanitized = sanitized.left(255);
    }
    
    return sanitized.trimmed();
}

Expected<bool, StorageError> StorageManager::performMigration(int targetVersion) {
    switch (targetVersion) {
        case 1:
            // Initial schema is already created in createTables()
            Logger::instance().info("Migration to version 1: Initial schema");
            return true;
            
        default:
            Logger::instance().error("Unknown migration target version: {}", targetVersion);
            return makeUnexpected(StorageError::QueryFailed);
    }
}

Expected<int, StorageError> StorageManager::getSchemaVersion() const {
    QMutexLocker locker(&databaseMutex_);
    
    if (!database_.isOpen()) {
        return makeUnexpected(StorageError::DatabaseNotOpen);
    }
    
    // Check if the version table exists first
    auto versionTableResult = executeScalar(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'"
    );
    
    if (versionTableResult.hasError()) {
        return makeUnexpected(versionTableResult.error());
    }
    
    if (!versionTableResult.value().isValid()) {
        // Version table doesn't exist, assume initial version
        return 1;
    }
    
    // Get the current schema version
    auto versionResult = executeScalar("SELECT version FROM schema_version ORDER BY id DESC LIMIT 1");
    if (versionResult.hasError()) {
        return makeUnexpected(versionResult.error());
    }
    
    if (!versionResult.value().isValid()) {
        // No version recorded, assume initial version
        return 1;
    }
    
    return versionResult.value().toInt();
}

} // namespace Murmur