#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtCore/QMutex>
#include <memory>
#include <functional>
#include <unordered_map>

#include "core/common/Expected.hpp"
#include "core/common/Logger.hpp"

namespace Murmur {

enum class CacheError {
    InitializationFailed,
    KeyNotFound,
    InvalidKey,
    CacheFull,
    WriteError,
    ReadError,
    CompressionError,
    DecompressionError,
    SerializationError,
    DeserializationError
};

enum class CachePolicy {
    LeastRecentlyUsed,
    LeastFrequentlyUsed,
    FirstInFirstOut,
    TimeToLive,
    WriteThrough,
    WriteBack
};

struct CacheEntry {
    QString key;
    QByteArray data;
    QDateTime createdAt;
    QDateTime lastAccessed;
    QDateTime lastModified;
    qint64 size;
    qint64 accessCount;
    qint64 ttl; // Time to live in seconds
    bool compressed;
    bool dirty; // For write-back policy
    QString filePath; // For persistent cache
    QByteArray checksum;
};

struct CacheStats {
    qint64 totalSize;
    qint64 maxSize;
    qint64 entryCount;
    qint64 maxEntries;
    qint64 hitCount;
    qint64 missCount;
    qint64 evictionCount;
    double hitRate;
    double missRate;
    QDateTime lastCleanup;
};

class FileCache : public QObject {
    Q_OBJECT

public:
    explicit FileCache(QObject* parent = nullptr);
    ~FileCache() override;

    // Initialization
    Expected<void, CacheError> initialize(const QString& cacheDir, qint64 maxSize = 1024 * 1024 * 100); // 100MB default
    Expected<void, CacheError> shutdown();
    bool isInitialized() const;

    // Cache operations
    Expected<void, CacheError> put(const QString& key, const QByteArray& data, qint64 ttl = -1);
    Expected<QByteArray, CacheError> get(const QString& key);
    Expected<bool, CacheError> contains(const QString& key);
    Expected<void, CacheError> remove(const QString& key);
    Expected<void, CacheError> clear();

    // File operations
    Expected<void, CacheError> putFile(const QString& key, const QString& filePath, qint64 ttl = -1);
    Expected<QString, CacheError> getFile(const QString& key, const QString& outputPath = QString());
    Expected<void, CacheError> cacheFile(const QString& filePath, const QString& key = QString());
    Expected<void, CacheError> uncacheFile(const QString& key);

    // Batch operations
    Expected<void, CacheError> putBatch(const QMap<QString, QByteArray>& entries, qint64 ttl = -1);
    Expected<QMap<QString, QByteArray>, CacheError> getBatch(const QStringList& keys);
    Expected<void, CacheError> removeBatch(const QStringList& keys);

    // Configuration
    Expected<void, CacheError> setMaxSize(qint64 maxSize);
    Expected<void, CacheError> setMaxEntries(qint64 maxEntries);
    Expected<void, CacheError> setCachePolicy(CachePolicy policy);
    Expected<void, CacheError> setCompressionEnabled(bool enabled);
    Expected<void, CacheError> setCompressionLevel(int level);
    Expected<void, CacheError> setPersistentCacheEnabled(bool enabled);
    Expected<void, CacheError> setCleanupInterval(int intervalMs);

    // Information
    Expected<CacheStats, CacheError> getStats() const;
    Expected<CacheEntry, CacheError> getEntry(const QString& key) const;
    Expected<QStringList, CacheError> getKeys() const;
    Expected<qint64, CacheError> getSize() const;
    Expected<qint64, CacheError> getEntryCount() const;

    // Maintenance
    Expected<void, CacheError> cleanup();
    Expected<void, CacheError> compact();
    Expected<void, CacheError> flush();
    Expected<void, CacheError> sync();
    Expected<qint64, CacheError> evict(qint64 targetSize);

    // Persistence
    Expected<void, CacheError> save(const QString& filePath = QString());
    Expected<void, CacheError> load(const QString& filePath = QString());
    Expected<void, CacheError> import(const QString& filePath);
    Expected<void, CacheError> export_(const QString& filePath);

signals:
    void entryAdded(const QString& key, qint64 size);
    void entryRemoved(const QString& key, qint64 size);
    void entryUpdated(const QString& key, qint64 oldSize, qint64 newSize);
    void entryAccessed(const QString& key);
    void cacheCleared();
    void cacheFull();
    void evictionOccurred(const QString& key, const QString& reason);
    void compressionCompleted(const QString& key, qint64 originalSize, qint64 compressedSize);
    void decompressionCompleted(const QString& key, qint64 compressedSize, qint64 originalSize);

private slots:
    void performCleanup();
    void performSync();

private:
    class FileCachePrivate;
    std::unique_ptr<FileCachePrivate> d;

    // Cache management
    Expected<void, CacheError> insertEntry(const QString& key, const QByteArray& data, qint64 ttl);
    Expected<void, CacheError> updateEntry(const QString& key, const QByteArray& data);
    Expected<void, CacheError> removeEntry(const QString& key);
    Expected<void, CacheError> evictEntry(const QString& key, const QString& reason);
    Expected<void, CacheError> makeRoom(qint64 requiredSize);

    // Compression
    Expected<QByteArray, CacheError> compressData(const QByteArray& data);
    Expected<QByteArray, CacheError> decompressData(const QByteArray& compressedData);

    // Serialization
    Expected<QByteArray, CacheError> serializeEntry(const CacheEntry& entry);
    Expected<CacheEntry, CacheError> deserializeEntry(const QByteArray& data);

    // Persistence
    Expected<void, CacheError> loadFromDisk(const QString& key);
    Expected<void, CacheError> saveToDisk(const QString& key);
    Expected<void, CacheError> removeFromDisk(const QString& key);

    // Utility
    Expected<void, CacheError> validateKey(const QString& key);
    Expected<void, CacheError> updateStats();
    Expected<void, CacheError> checkExpiration(const QString& key);
    Expected<QString, CacheError> generateCacheFilePath(const QString& key);
    Expected<QByteArray, CacheError> calculateChecksum(const QByteArray& data);
    Expected<bool, CacheError> verifyChecksum(const QByteArray& data, const QByteArray& checksum);
    QString sanitizeKey(const QString& key);
};

} // namespace Murmur