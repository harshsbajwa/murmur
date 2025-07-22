#include "FileCache.hpp"
#include "core/common/Logger.hpp"
#include "core/security/InputValidator.hpp"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDataStream>
#include <QtCore/QCryptographicHash>
#include <QtCore/QStandardPaths>
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>
#include <QtCore/QRandomGenerator>
#include <QtCore/QRegularExpression>
#include <QCoreApplication>
#include <algorithm>
#include <chrono>

namespace Murmur {

class FileCache::FileCachePrivate {
public:
    FileCachePrivate() = default;
    ~FileCachePrivate() = default;

    bool initialized = false;
    QString cacheDirectory;
    qint64 maxSize = 1024 * 1024 * 100; // 100MB default
    qint64 maxEntries = 10000;
    CachePolicy policy = CachePolicy::LeastRecentlyUsed;
    bool compressionEnabled = true;
    int compressionLevel = 6;
    bool persistentCacheEnabled = true;
    int cleanupInterval = 300000; // 5 minutes

    std::unordered_map<QString, std::unique_ptr<CacheEntry>> entries;
    CacheStats stats;
    mutable QMutex mutex;
    std::unique_ptr<QTimer> cleanupTimer;
    std::unique_ptr<QTimer> syncTimer;
    std::unique_ptr<InputValidator> validator;
};

FileCache::FileCache(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<FileCachePrivate>())
{
    d->validator = std::make_unique<InputValidator>();
    
    // Initialize stats
    d->stats.totalSize = 0;
    d->stats.maxSize = d->maxSize;
    d->stats.entryCount = 0;
    d->stats.maxEntries = d->maxEntries;
    d->stats.hitCount = 0;
    d->stats.missCount = 0;
    d->stats.evictionCount = 0;
    d->stats.hitRate = 0.0;
    d->stats.missRate = 0.0;
    d->stats.lastCleanup = QDateTime::currentDateTime();

    // Set up cleanup timer
    d->cleanupTimer = std::make_unique<QTimer>(this);
    connect(d->cleanupTimer.get(), &QTimer::timeout, this, &FileCache::performCleanup);

    // Set up sync timer
    d->syncTimer = std::make_unique<QTimer>(this);
    connect(d->syncTimer.get(), &QTimer::timeout, this, &FileCache::performSync);
}

FileCache::~FileCache() {
    if (d->initialized) {
        shutdown();
    }
}

Expected<void, CacheError> FileCache::initialize(const QString& cacheDir, qint64 maxSize) {
    QMutexLocker locker(&d->mutex);
    
    if (d->initialized) {
        return Expected<void, CacheError>();
    }

    if (cacheDir.isEmpty()) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    QDir dir(cacheDir);
    if (!dir.exists()) {
        if (!dir.mkpath(cacheDir)) {
            Logger::instance().error("Failed to create cache directory: {}", cacheDir.toStdString());
            return makeUnexpected(CacheError::InitializationFailed);
        }
    }

    d->cacheDirectory = cacheDir;
    d->maxSize = maxSize;
    d->stats.maxSize = maxSize;
    d->initialized = true;

    // Start timers
    d->cleanupTimer->start(d->cleanupInterval);
    d->syncTimer->start(60000); // Sync every minute

    // Load existing cache entries if persistent cache is enabled
    if (d->persistentCacheEnabled) {
        QString cacheIndexPath = QDir(d->cacheDirectory).filePath("cache_index.dat");
        if (QFile::exists(cacheIndexPath)) {
            auto loadResult = load(cacheIndexPath);
            if (!loadResult.hasValue()) {
                Logger::instance().warn("Failed to load cache index, starting with empty cache");
            }
        }
    }

    Logger::instance().info("FileCache initialized: dir={}, maxSize={}", 
                           cacheDir.toStdString(), maxSize);
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::shutdown() {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return Expected<void, CacheError>();
    }

    // Stop timers
    d->cleanupTimer->stop();
    d->syncTimer->stop();

    // Save cache index if persistent cache is enabled
    if (d->persistentCacheEnabled) {
        QString cacheIndexPath = QDir(d->cacheDirectory).filePath("cache_index.dat");
        auto saveResult = save(cacheIndexPath);
        if (!saveResult.hasValue()) {
            Logger::instance().warn("Failed to save cache index during shutdown");
        }
    }

    // Clear entries
    d->entries.clear();
    d->initialized = false;

    Logger::instance().info("FileCache shut down");
    return Expected<void, CacheError>();
}

bool FileCache::isInitialized() const {
    QMutexLocker locker(&d->mutex);
    return d->initialized;
}

Expected<void, CacheError> FileCache::put(const QString& key, const QByteArray& data, qint64 ttl) {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    auto keyValidation = validateKey(key);
    if (!keyValidation.hasValue()) {
        return keyValidation;
    }

    return insertEntry(key, data, ttl);
}

Expected<QByteArray, CacheError> FileCache::get(const QString& key) {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    auto keyValidation = validateKey(key);
    if (!keyValidation.hasValue()) {
        return makeUnexpected(CacheError::InvalidKey);
    }

    auto it = d->entries.find(key);
    if (it == d->entries.end()) {
        // Try to load from disk if persistent cache is enabled
        if (d->persistentCacheEnabled) {
            auto loadResult = loadFromDisk(key);
            if (loadResult.hasValue()) {
                it = d->entries.find(key);
            }
        }
        
        if (it == d->entries.end()) {
            d->stats.missCount++;
            updateStats();
            return makeUnexpected(CacheError::KeyNotFound);
        }
    }

    auto& entry = it->second;
    
    // Check expiration
    if (entry->ttl > 0) {
        auto now = QDateTime::currentDateTime();
        if (entry->createdAt.addSecs(entry->ttl) < now) {
            removeEntry(key);
            d->stats.missCount++;
            updateStats();
            return makeUnexpected(CacheError::KeyNotFound);
        }
    }

    // Update access information
    entry->lastAccessed = QDateTime::currentDateTime();
    entry->accessCount++;
    
    d->stats.hitCount++;
    updateStats();
    
    emit entryAccessed(key);

    QByteArray result = entry->data;
    
    // Decompress if needed
    if (entry->compressed) {
        auto decompressed = decompressData(result);
        if (!decompressed.hasValue()) {
            return makeUnexpected(CacheError::DecompressionError);
        }
        result = decompressed.value();
    }

    return result;
}

Expected<bool, CacheError> FileCache::contains(const QString& key) {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    auto keyValidation = validateKey(key);
    if (!keyValidation.hasValue()) {
        return false;
    }

    auto it = d->entries.find(key);
    if (it == d->entries.end()) {
        // Try to load from disk if persistent cache is enabled
        if (d->persistentCacheEnabled) {
            auto loadResult = loadFromDisk(key);
            if (loadResult.hasValue()) {
                it = d->entries.find(key);
            }
        }
        
        if (it == d->entries.end()) {
            return false;
        }
    }

    // Check expiration
    if (it->second->ttl > 0) {
        auto now = QDateTime::currentDateTime();
        if (it->second->createdAt.addSecs(it->second->ttl) < now) {
            removeEntry(key);
            return false;
        }
    }

    return true;
}

Expected<void, CacheError> FileCache::remove(const QString& key) {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    auto keyValidation = validateKey(key);
    if (!keyValidation.hasValue()) {
        return keyValidation;
    }

    return removeEntry(key);
}

Expected<void, CacheError> FileCache::clear() {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    // Remove all disk files if persistent cache is enabled
    if (d->persistentCacheEnabled) {
        for (const auto& [key, entry] : d->entries) {
            removeFromDisk(key);
        }
    }

    d->entries.clear();
    d->stats.totalSize = 0;
    d->stats.entryCount = 0;
    d->stats.evictionCount = 0;
    
    updateStats();
    
    Logger::instance().info("FileCache cleared");
    emit cacheCleared();
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::putFile(const QString& key, const QString& filePath, qint64 ttl) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return makeUnexpected(CacheError::ReadError);
    }

    QByteArray data = file.readAll();
    return put(key, data, ttl);
}

Expected<QString, CacheError> FileCache::getFile(const QString& key, const QString& outputPath) {
    auto data = get(key);
    if (!data.hasValue()) {
        return makeUnexpected(data.error());
    }

    QString filePath = outputPath;
    if (filePath.isEmpty()) {
        filePath = QDir(d->cacheDirectory).filePath(sanitizeKey(key));
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return makeUnexpected(CacheError::WriteError);
    }

    if (file.write(data.value()) != data.value().size()) {
        return makeUnexpected(CacheError::WriteError);
    }

    return filePath;
}

Expected<void, CacheError> FileCache::cacheFile(const QString& filePath, const QString& key) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        return makeUnexpected(CacheError::ReadError);
    }

    QString cacheKey = key.isEmpty() ? fileInfo.fileName() : key;
    return putFile(cacheKey, filePath);
}

Expected<void, CacheError> FileCache::uncacheFile(const QString& key) {
    return remove(key);
}

Expected<void, CacheError> FileCache::putBatch(const QMap<QString, QByteArray>& entries, qint64 ttl) {
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        auto result = put(it.key(), it.value(), ttl);
        if (!result.hasValue()) {
            return result;
        }
    }
    return Expected<void, CacheError>();
}

Expected<QMap<QString, QByteArray>, CacheError> FileCache::getBatch(const QStringList& keys) {
    QMap<QString, QByteArray> results;
    
    for (const QString& key : keys) {
        auto result = get(key);
        if (result.hasValue()) {
            results[key] = result.value();
        }
    }
    
    return results;
}

Expected<void, CacheError> FileCache::removeBatch(const QStringList& keys) {
    for (const QString& key : keys) {
        auto result = remove(key);
        if (!result.hasValue()) {
            return result;
        }
    }
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::setMaxSize(qint64 maxSize) {
    QMutexLocker locker(&d->mutex);
    
    if (maxSize <= 0) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    d->maxSize = maxSize;
    d->stats.maxSize = maxSize;

    // Trigger eviction if current size exceeds new limit
    if (d->stats.totalSize > maxSize) {
        evict(maxSize);
    }

    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::setMaxEntries(qint64 maxEntries) {
    QMutexLocker locker(&d->mutex);
    
    if (maxEntries <= 0) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    d->maxEntries = maxEntries;
    d->stats.maxEntries = maxEntries;

    // Trigger eviction if current count exceeds new limit
    while (d->stats.entryCount > maxEntries) {
        auto evictResult = evict(d->stats.totalSize - 1);
        if (!evictResult.hasValue()) {
            break;
        }
    }

    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::setCachePolicy(CachePolicy policy) {
    QMutexLocker locker(&d->mutex);
    d->policy = policy;
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::setCompressionEnabled(bool enabled) {
    QMutexLocker locker(&d->mutex);
    d->compressionEnabled = enabled;
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::setCompressionLevel(int level) {
    QMutexLocker locker(&d->mutex);
    
    if (level < 1 || level > 9) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    d->compressionLevel = level;
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::setPersistentCacheEnabled(bool enabled) {
    QMutexLocker locker(&d->mutex);
    d->persistentCacheEnabled = enabled;
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::setCleanupInterval(int intervalMs) {
    QMutexLocker locker(&d->mutex);
    
    if (intervalMs <= 0) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    d->cleanupInterval = intervalMs;
    if (d->cleanupTimer) {
        d->cleanupTimer->setInterval(intervalMs);
    }
    
    return Expected<void, CacheError>();
}

Expected<CacheStats, CacheError> FileCache::getStats() const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    return d->stats;
}

Expected<CacheEntry, CacheError> FileCache::getEntry(const QString& key) const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    auto it = d->entries.find(key);
    if (it == d->entries.end()) {
        return makeUnexpected(CacheError::KeyNotFound);
    }

    return *it->second;
}

Expected<QStringList, CacheError> FileCache::getKeys() const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    QStringList keys;
    for (const auto& [key, entry] : d->entries) {
        keys.append(key);
    }
    return keys;
}

Expected<qint64, CacheError> FileCache::getSize() const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    return d->stats.totalSize;
}

Expected<qint64, CacheError> FileCache::getEntryCount() const {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    return d->stats.entryCount;
}

Expected<void, CacheError> FileCache::cleanup() {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    auto now = QDateTime::currentDateTime();
    QStringList expiredKeys;

    // Find expired entries
    for (const auto& [key, entry] : d->entries) {
        if (entry->ttl > 0 && entry->createdAt.addSecs(entry->ttl) < now) {
            expiredKeys.append(key);
        }
    }

    // Remove expired entries
    for (const QString& key : expiredKeys) {
        removeEntry(key);
    }

    d->stats.lastCleanup = now;
    updateStats();

    Logger::instance().info("FileCache cleanup completed, removed {} expired entries", expiredKeys.size());
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::compact() {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    // Compact by removing fragmentation and reorganizing data
    // This is a simplified implementation
    qint64 originalSize = d->stats.totalSize;
    
    // Recompress entries that might benefit from better compression
    for (const auto& [key, entry] : d->entries) {
        if (entry->compressed && entry->data.size() > 1024) {
            auto decompressed = decompressData(entry->data);
            if (decompressed.hasValue()) {
                auto recompressed = compressData(decompressed.value());
                if (recompressed.hasValue() && recompressed.value().size() < entry->data.size()) {
                    qint64 oldSize = entry->size;
                    entry->data = recompressed.value();
                    entry->size = entry->data.size();
                    d->stats.totalSize = d->stats.totalSize - oldSize + entry->size;
                }
            }
        }
    }

    qint64 savedBytes = originalSize - d->stats.totalSize;
    Logger::instance().info("FileCache compaction completed, saved {} bytes", savedBytes);
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::flush() {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    // Flush dirty entries to disk if using write-back policy
    if (d->policy == CachePolicy::WriteBack && d->persistentCacheEnabled) {
        for (const auto& [key, entry] : d->entries) {
            if (entry->dirty) {
                auto saveResult = saveToDisk(key);
                if (saveResult.hasValue()) {
                    entry->dirty = false;
                }
            }
        }
    }

    Logger::instance().info("FileCache flush completed");
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::sync() {
    return flush();
}

Expected<qint64, CacheError> FileCache::evict(qint64 targetSize) {
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    qint64 bytesEvicted = 0;
    
    while (d->stats.totalSize > targetSize && !d->entries.empty()) {
        QString keyToEvict;
        
        switch (d->policy) {
            case CachePolicy::LeastRecentlyUsed: {
                QDateTime oldest = QDateTime::currentDateTime();
                for (const auto& [key, entry] : d->entries) {
                    if (entry->lastAccessed < oldest) {
                        oldest = entry->lastAccessed;
                        keyToEvict = key;
                    }
                }
                break;
            }
            case CachePolicy::LeastFrequentlyUsed: {
                qint64 minAccess = std::numeric_limits<qint64>::max();
                for (const auto& [key, entry] : d->entries) {
                    if (entry->accessCount < minAccess) {
                        minAccess = entry->accessCount;
                        keyToEvict = key;
                    }
                }
                break;
            }
            case CachePolicy::FirstInFirstOut: {
                QDateTime oldest = QDateTime::currentDateTime();
                for (const auto& [key, entry] : d->entries) {
                    if (entry->createdAt < oldest) {
                        oldest = entry->createdAt;
                        keyToEvict = key;
                    }
                }
                break;
            }
            default:
                keyToEvict = d->entries.begin()->first;
                break;
        }

        if (keyToEvict.isEmpty()) {
            break;
        }

        auto it = d->entries.find(keyToEvict);
        if (it != d->entries.end()) {
            bytesEvicted += it->second->size;
            evictEntry(keyToEvict, "Size limit exceeded");
        }
    }

    return bytesEvicted;
}

Expected<void, CacheError> FileCache::save(const QString& filePath) {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    QString path = filePath.isEmpty() ? QDir(d->cacheDirectory).filePath("cache_index.dat") : filePath;
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return makeUnexpected(CacheError::WriteError);
    }

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);

    // Write header
    stream << QString("FileCache");
    stream << static_cast<quint32>(1); // Version
    stream << static_cast<quint64>(d->entries.size());

    // Write entries
    for (const auto& [key, entry] : d->entries) {
        auto serialized = serializeEntry(*entry);
        if (!serialized.hasValue()) {
            return makeUnexpected(CacheError::SerializationError);
        }
        stream << key << serialized.value();
    }

    Logger::instance().info("FileCache saved to {}", path.toStdString());
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::load(const QString& filePath) {
    QMutexLocker locker(&d->mutex);
    
    if (!d->initialized) {
        return makeUnexpected(CacheError::InitializationFailed);
    }

    QString path = filePath.isEmpty() ? QDir(d->cacheDirectory).filePath("cache_index.dat") : filePath;
    
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return makeUnexpected(CacheError::ReadError);
    }

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);

    // Read header
    QString header;
    quint32 version;
    quint64 entryCount;
    
    stream >> header >> version >> entryCount;
    
    if (header != "FileCache" || version != 1) {
        return makeUnexpected(CacheError::DeserializationError);
    }

    // Read entries
    for (quint64 i = 0; i < entryCount; ++i) {
        QString key;
        QByteArray serializedEntry;
        
        stream >> key >> serializedEntry;
        
        auto entry = deserializeEntry(serializedEntry);
        if (!entry.hasValue()) {
            continue; // Skip corrupted entries
        }

        d->entries[key] = std::make_unique<CacheEntry>(entry.value());
        d->stats.totalSize += entry.value().size;
        d->stats.entryCount++;
    }

    updateStats();
    Logger::instance().info("FileCache loaded from {}, {} entries", path.toStdString(), d->entries.size());
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::import(const QString& filePath) {
    return load(filePath);
}

Expected<void, CacheError> FileCache::export_(const QString& filePath) {
    return save(filePath);
}

void FileCache::performCleanup() {
    cleanup();
}

void FileCache::performSync() {
    sync();
}

Expected<void, CacheError> FileCache::insertEntry(const QString& key, const QByteArray& data, qint64 ttl) {
    qint64 dataSize = data.size();
    
    // Check if we need to make room
    if (d->stats.totalSize + dataSize > d->maxSize) {
        auto makeRoomResult = makeRoom(dataSize);
        if (!makeRoomResult.hasValue()) {
            return makeRoomResult;
        }
    }

    // Check entry count limit
    if (d->stats.entryCount >= d->maxEntries) {
        auto evictResult = evict(d->stats.totalSize - 1);
        if (!evictResult.hasValue()) {
            return makeUnexpected(CacheError::CacheFull);
        }
    }

    auto entry = std::make_unique<CacheEntry>();
    entry->key = key;
    entry->data = data;
    entry->createdAt = QDateTime::currentDateTime();
    entry->lastAccessed = entry->createdAt;
    entry->lastModified = entry->createdAt;
    entry->size = dataSize;
    entry->accessCount = 1;
    entry->ttl = ttl;
    entry->compressed = false;
    entry->dirty = false;

    // Compress if enabled and data is large enough
    if (d->compressionEnabled && data.size() > 1024) {
        auto compressed = compressData(data);
        if (compressed.hasValue()) {
            entry->data = compressed.value();
            entry->size = entry->data.size();
            entry->compressed = true;
            
            emit compressionCompleted(key, dataSize, entry->size);
        }
    }

    // Calculate checksum
    auto checksum = calculateChecksum(data);
    if (checksum.hasValue()) {
        entry->checksum = checksum.value();
    }

    // Save to disk if persistent cache is enabled
    if (d->persistentCacheEnabled) {
        auto saveResult = saveToDisk(key);
        if (saveResult.hasValue()) {
            auto filePath = generateCacheFilePath(key);
            if (filePath.hasValue()) {
                entry->filePath = filePath.value();
            }
        }
    }

    // Check if entry already exists
    auto existingIt = d->entries.find(key);
    if (existingIt != d->entries.end()) {
        qint64 oldSize = existingIt->second->size;
        d->stats.totalSize = d->stats.totalSize - oldSize + entry->size;
        d->entries[key] = std::move(entry);
        emit entryUpdated(key, oldSize, entry->size);
    } else {
        d->stats.totalSize += entry->size;
        d->stats.entryCount++;
        d->entries[key] = std::move(entry);
        emit entryAdded(key, entry->size);
    }

    updateStats();
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::updateEntry(const QString& key, const QByteArray& data) {
    auto it = d->entries.find(key);
    if (it == d->entries.end()) {
        return makeUnexpected(CacheError::KeyNotFound);
    }

    auto& entry = it->second;
    qint64 oldSize = entry->size;
    
    entry->data = data;
    entry->lastModified = QDateTime::currentDateTime();
    entry->size = data.size();
    entry->dirty = true;

    // Recompress if needed
    if (d->compressionEnabled && entry->compressed && data.size() > 1024) {
        auto compressed = compressData(data);
        if (compressed.hasValue()) {
            entry->data = compressed.value();
            entry->size = entry->data.size();
        }
    }

    d->stats.totalSize = d->stats.totalSize - oldSize + entry->size;
    updateStats();
    
    emit entryUpdated(key, oldSize, entry->size);
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::removeEntry(const QString& key) {
    auto it = d->entries.find(key);
    if (it == d->entries.end()) {
        return makeUnexpected(CacheError::KeyNotFound);
    }

    qint64 size = it->second->size;
    
    // Remove from disk if persistent cache is enabled
    if (d->persistentCacheEnabled) {
        removeFromDisk(key);
    }

    d->entries.erase(it);
    d->stats.totalSize -= size;
    d->stats.entryCount--;
    
    updateStats();
    
    emit entryRemoved(key, size);
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::evictEntry(const QString& key, const QString& reason) {
    auto removeResult = removeEntry(key);
    if (removeResult.hasValue()) {
        d->stats.evictionCount++;
        updateStats();
        emit evictionOccurred(key, reason);
    }
    return removeResult;
}

Expected<void, CacheError> FileCache::makeRoom(qint64 requiredSize) {
    qint64 targetSize = d->maxSize - requiredSize;
    auto result = evict(targetSize);
    if (!result.hasValue()) {
        return makeUnexpected(result.error());
    }
    return Expected<void, CacheError>();
}

Expected<QByteArray, CacheError> FileCache::compressData(const QByteArray& data) {
    // Simplified compression using qCompress
    QByteArray compressed = qCompress(data, d->compressionLevel);
    if (compressed.isEmpty()) {
        return makeUnexpected(CacheError::CompressionError);
    }
    return compressed;
}

Expected<QByteArray, CacheError> FileCache::decompressData(const QByteArray& compressedData) {
    // Simplified decompression using qUncompress
    QByteArray decompressed = qUncompress(compressedData);
    if (decompressed.isEmpty()) {
        return makeUnexpected(CacheError::DecompressionError);
    }
    return decompressed;
}

Expected<QByteArray, CacheError> FileCache::serializeEntry(const CacheEntry& entry) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);

    stream << entry.key;
    stream << entry.data;
    stream << entry.createdAt;
    stream << entry.lastAccessed;
    stream << entry.lastModified;
    stream << entry.size;
    stream << entry.accessCount;
    stream << entry.ttl;
    stream << entry.compressed;
    stream << entry.dirty;
    stream << entry.filePath;
    stream << entry.checksum;

    return data;
}

Expected<CacheEntry, CacheError> FileCache::deserializeEntry(const QByteArray& data) {
    QDataStream stream(data);
    stream.setVersion(QDataStream::Qt_6_0);

    CacheEntry entry;
    stream >> entry.key;
    stream >> entry.data;
    stream >> entry.createdAt;
    stream >> entry.lastAccessed;
    stream >> entry.lastModified;
    stream >> entry.size;
    stream >> entry.accessCount;
    stream >> entry.ttl;
    stream >> entry.compressed;
    stream >> entry.dirty;
    stream >> entry.filePath;
    stream >> entry.checksum;

    return entry;
}

Expected<void, CacheError> FileCache::loadFromDisk(const QString& key) {
    if (!d->persistentCacheEnabled) {
        return makeUnexpected(CacheError::ReadError);
    }

    auto filePath = generateCacheFilePath(key);
    if (!filePath.hasValue()) {
        return makeUnexpected(CacheError::ReadError);
    }

    QFile file(filePath.value());
    if (!file.open(QIODevice::ReadOnly)) {
        return makeUnexpected(CacheError::ReadError);
    }

    QByteArray data = file.readAll();
    return insertEntry(key, data, -1);
}

Expected<void, CacheError> FileCache::saveToDisk(const QString& key) {
    if (!d->persistentCacheEnabled) {
        return Expected<void, CacheError>();
    }

    auto it = d->entries.find(key);
    if (it == d->entries.end()) {
        return makeUnexpected(CacheError::KeyNotFound);
    }

    auto filePath = generateCacheFilePath(key);
    if (!filePath.hasValue()) {
        return makeUnexpected(CacheError::WriteError);
    }

    QFile file(filePath.value());
    if (!file.open(QIODevice::WriteOnly)) {
        return makeUnexpected(CacheError::WriteError);
    }

    if (file.write(it->second->data) != it->second->data.size()) {
        return makeUnexpected(CacheError::WriteError);
    }

    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::removeFromDisk(const QString& key) {
    if (!d->persistentCacheEnabled) {
        return Expected<void, CacheError>();
    }

    auto filePath = generateCacheFilePath(key);
    if (!filePath.hasValue()) {
        return Expected<void, CacheError>();
    }

    QFile::remove(filePath.value());
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::validateKey(const QString& key) {
    if (!d->validator || !d->validator->isValidCacheKey(key)) {
        return makeUnexpected(CacheError::InvalidKey);
    }
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::updateStats() {
    qint64 total = d->stats.hitCount + d->stats.missCount;
    if (total > 0) {
        d->stats.hitRate = static_cast<double>(d->stats.hitCount) / total;
        d->stats.missRate = static_cast<double>(d->stats.missCount) / total;
    }
    return Expected<void, CacheError>();
}

Expected<void, CacheError> FileCache::checkExpiration(const QString& key) {
    auto it = d->entries.find(key);
    if (it == d->entries.end()) {
        return makeUnexpected(CacheError::KeyNotFound);
    }

    if (it->second->ttl > 0) {
        auto now = QDateTime::currentDateTime();
        if (it->second->createdAt.addSecs(it->second->ttl) < now) {
            return removeEntry(key);
        }
    }

    return Expected<void, CacheError>();
}

Expected<QString, CacheError> FileCache::generateCacheFilePath(const QString& key) {
    QString sanitized = sanitizeKey(key);
    return QDir(d->cacheDirectory).filePath(sanitized + ".cache");
}

Expected<QByteArray, CacheError> FileCache::calculateChecksum(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Md5);
}

Expected<bool, CacheError> FileCache::verifyChecksum(const QByteArray& data, const QByteArray& checksum) {
    auto calculated = calculateChecksum(data);
    if (!calculated.hasValue()) {
        return false;
    }
    return calculated.value() == checksum;
}

QString FileCache::sanitizeKey(const QString& key) {
    QString sanitized = key;
    sanitized.replace(QRegularExpression("[^a-zA-Z0-9_\\-]"), "_");
    return sanitized;
}

} // namespace Murmur

