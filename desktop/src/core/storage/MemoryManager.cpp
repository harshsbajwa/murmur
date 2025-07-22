#include "MemoryManager.hpp"
#include "core/common/Logger.hpp"

#include <QtCore/QTimer>
#include <QtCore/QMutexLocker>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QCryptographicHash>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <new>

namespace Murmur {

class MemoryManager::MemoryManagerPrivate {
public:
    MemoryManagerPrivate() = default;
    ~MemoryManagerPrivate() = default;

    bool initialized = false;
    size_t totalMemoryLimit = 1024 * 1024 * 1024; // 1GB default
    size_t currentUsage = 0;
    size_t peakUsage = 0;
    double memoryPressureThreshold = 0.8; // 80%
    bool debugMode = false;
    int garbageCollectionInterval = 60000; // 1 minute

    std::unordered_map<MemoryPoolType, std::unique_ptr<MemoryPool>> pools;
    std::unordered_map<void*, MemoryPoolType> pointerToPool;
    MemoryStats stats;
    mutable QMutex mutex;

    std::unique_ptr<QTimer> gcTimer;
    std::unique_ptr<QTimer> pressureTimer;
    std::unique_ptr<QTimer> leakTimer;

    MemoryPressureCallback pressureCallback;
    OutOfMemoryCallback oomCallback;
    LeakDetectionCallback leakCallback;
};

MemoryManager::MemoryManager(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<MemoryManagerPrivate>())
{
    // Initialize stats
    d->stats.totalAllocated = 0;
    d->stats.totalFreed = 0;
    d->stats.currentUsage = 0;
    d->stats.peakUsage = 0;
    d->stats.allocationCount = 0;
    d->stats.freeCount = 0;
    d->stats.activeBlocks = 0;
    d->stats.poolCount = 0;
    d->stats.fragmentationRatio = 0.0;
    d->stats.largestFreeBlock = 0;
    d->stats.smallestFreeBlock = 0;
    d->stats.lastReset = std::chrono::steady_clock::now();

    // Set up timers
    d->gcTimer = std::make_unique<QTimer>(this);
    connect(d->gcTimer.get(), &QTimer::timeout, this, &MemoryManager::performGarbageCollection);

    d->pressureTimer = std::make_unique<QTimer>(this);
    connect(d->pressureTimer.get(), &QTimer::timeout, this, &MemoryManager::checkMemoryPressure);

    d->leakTimer = std::make_unique<QTimer>(this);
    connect(d->leakTimer.get(), &QTimer::timeout, this, &MemoryManager::performLeakDetection);
}

MemoryManager::~MemoryManager() {
    if (d->initialized) {
        shutdown();
    }
}

Expected<void, MemoryError> MemoryManager::initialize(size_t totalMemoryLimit) {
    QMutexLocker locker(&d->mutex);

    if (d->initialized) {
        return Expected<void, MemoryError>();
    }

    if (totalMemoryLimit == 0) {
        return makeUnexpected(MemoryError::InvalidSize);
    }

    d->totalMemoryLimit = totalMemoryLimit;
    d->currentUsage = 0;
    d->peakUsage = 0;

    // Create default pools
    auto pools = {
        MemoryPoolType::General,
        MemoryPoolType::Video,
        MemoryPoolType::Audio,
        MemoryPoolType::Transcription,
        MemoryPoolType::Torrent,
        MemoryPoolType::Temporary,
        MemoryPoolType::Large
    };

    for (auto poolType : pools) {
        size_t poolSize = totalMemoryLimit / 7; // Divide equally among pools
        if (poolType == MemoryPoolType::Large) {
            poolSize = totalMemoryLimit / 4; // Larger pool for large allocations
        }
        
        auto result = createPool(poolType, poolSize);
        if (!result.hasValue()) {
            Logger::instance().warn("Failed to create pool for type {}", static_cast<int>(poolType));
        }
    }

    d->initialized = true;

    // Start timers
    d->gcTimer->start(d->garbageCollectionInterval);
    d->pressureTimer->start(5000); // Check pressure every 5 seconds
    d->leakTimer->start(300000); // Check for leaks every 5 minutes

    Logger::instance().info("MemoryManager initialized with limit: {} bytes", totalMemoryLimit);
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::shutdown() {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return Expected<void, MemoryError>();
    }

    // Stop timers
    d->gcTimer->stop();
    d->pressureTimer->stop();
    d->leakTimer->stop();

    // Perform final leak detection
    detectLeaks();

    // Free all pools
    for (auto& [type, pool] : d->pools) {
        for (auto& [ptr, block] : pool->blocks) {
            if (block->isActive) {
                std::free(block->ptr);
                emit memoryFreed(block->size, block->ptr);
            }
        }
        pool->blocks.clear();
        
        if (pool->basePtr) {
            std::free(pool->basePtr);
        }
    }

    d->pools.clear();
    d->pointerToPool.clear();
    d->currentUsage = 0;
    d->initialized = false;

    Logger::instance().info("MemoryManager shut down");
    return Expected<void, MemoryError>();
}

bool MemoryManager::isInitialized() const {
    QMutexLocker locker(&d->mutex);
    return d->initialized;
}

Expected<void*, MemoryError> MemoryManager::allocate(size_t size, size_t alignment, MemoryPoolType poolType, const std::string& allocatedBy) {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return makeUnexpected(MemoryError::InitializationFailed);
    }

    auto sizeValidation = validateSize(size);
    if (!sizeValidation.hasValue()) {
        return makeUnexpected(sizeValidation.error());
    }

    auto alignmentValidation = validateAlignment(alignment);
    if (!alignmentValidation.hasValue()) {
        return makeUnexpected(alignmentValidation.error());
    }

    return internalAllocate(size, alignment, poolType, allocatedBy);
}

Expected<void, MemoryError> MemoryManager::deallocate(void* ptr) {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return makeUnexpected(MemoryError::InitializationFailed);
    }

    if (!ptr) {
        return Expected<void, MemoryError>();
    }

    auto validation = validatePointer(ptr);
    if (!validation.hasValue()) {
        return makeUnexpected(validation.error());
    }

    return internalDeallocate(ptr);
}

Expected<void*, MemoryError> MemoryManager::reallocate(void* ptr, size_t newSize, size_t alignment) {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return makeUnexpected(MemoryError::InitializationFailed);
    }

    if (!ptr) {
        return allocate(newSize, alignment);
    }

    auto sizeValidation = validateSize(newSize);
    if (!sizeValidation.hasValue()) {
        return makeUnexpected(sizeValidation.error());
    }

    // Find the existing block
    auto poolResult = findPoolForPointer(ptr);
    if (!poolResult.hasValue()) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    auto pool = poolResult.value();
    auto blockIt = pool->blocks.find(ptr);
    if (blockIt == pool->blocks.end()) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    auto& block = blockIt->second;
    size_t oldSize = block->size;

    // Allocate new block
    auto newPtr = internalAllocate(newSize, alignment, pool->type, block->allocatedBy);
    if (!newPtr.hasValue()) {
        return makeUnexpected(newPtr.error());
    }

    // Copy data
    size_t copySize = std::min(oldSize, newSize);
    std::memcpy(newPtr.value(), ptr, copySize);

    // Free old block
    auto freeResult = internalDeallocate(ptr);
    if (!freeResult.hasValue()) {
        Logger::instance().warn("Failed to free old block during reallocation");
    }

    return newPtr.value();
}

Expected<void*, MemoryError> MemoryManager::alignedAllocate(size_t size, size_t alignment, MemoryPoolType poolType) {
    return allocate(size, alignment, poolType);
}

Expected<void, MemoryError> MemoryManager::createPool(MemoryPoolType type, size_t size, size_t alignment) {
    if (size == 0) {
        return makeUnexpected(MemoryError::InvalidSize);
    }

    auto alignmentValidation = validateAlignment(alignment);
    if (!alignmentValidation.hasValue()) {
        return makeUnexpected(alignmentValidation.error());
    }

    auto pool = std::make_unique<MemoryPool>();
    pool->type = type;
    pool->totalSize = size;
    pool->usedSize = 0;
    pool->availableSize = size;
    pool->blockCount = 0;
    pool->maxBlockSize = size / 2; // Max block is half the pool size
    pool->basePtr = nullptr;
    pool->isActive = true;
    pool->alignment = alignment;
    pool->createdAt = std::chrono::steady_clock::now();

    d->pools[type] = std::move(pool);
    d->stats.poolCount++;

    Logger::instance().info("Created memory pool type {}, size: {}", static_cast<int>(type), size);
    emit poolCreated(type, size);
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::destroyPool(MemoryPoolType type) {
    auto it = d->pools.find(type);
    if (it == d->pools.end()) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    auto& pool = it->second;
    
    // Free all blocks in the pool
    for (auto& [ptr, block] : pool->blocks) {
        if (block->isActive) {
            std::free(block->ptr);
            emit memoryFreed(block->size, block->ptr);
        }
    }

    // Remove pointer mappings
    for (auto& [ptr, block] : pool->blocks) {
        d->pointerToPool.erase(ptr);
    }

    if (pool->basePtr) {
        std::free(pool->basePtr);
    }

    d->pools.erase(it);
    d->stats.poolCount--;

    Logger::instance().info("Destroyed memory pool type {}", static_cast<int>(type));
    emit poolDestroyed(type);
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::resizePool(MemoryPoolType type, size_t newSize) {
    auto poolResult = findPool(type);
    if (!poolResult.hasValue()) {
        return makeUnexpected(poolResult.error());
    }

    auto pool = poolResult.value();
    
    if (newSize < pool->usedSize) {
        return makeUnexpected(MemoryError::InvalidSize);
    }

    pool->totalSize = newSize;
    pool->availableSize = newSize - pool->usedSize;
    pool->maxBlockSize = newSize / 2;

    Logger::instance().info("Resized memory pool type {} to {}", static_cast<int>(type), newSize);
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::clearPool(MemoryPoolType type) {
    auto poolResult = findPool(type);
    if (!poolResult.hasValue()) {
        return makeUnexpected(poolResult.error());
    }

    auto pool = poolResult.value();
    
    // Free all blocks
    for (auto& [ptr, block] : pool->blocks) {
        if (block->isActive) {
            std::free(block->ptr);
            emit memoryFreed(block->size, block->ptr);
        }
        d->pointerToPool.erase(ptr);
    }

    pool->blocks.clear();
    pool->usedSize = 0;
    pool->availableSize = pool->totalSize;
    pool->blockCount = 0;

    Logger::instance().info("Cleared memory pool type {}", static_cast<int>(type));
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::memorySet(void* ptr, int value, size_t size) {
    if (!ptr) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    auto validation = validatePointer(ptr);
    if (!validation.hasValue()) {
        return makeUnexpected(validation.error());
    }

    std::memset(ptr, value, size);
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::memoryCopy(void* dest, const void* src, size_t size) {
    if (!dest || !src) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    auto destValidation = validatePointer(dest);
    if (!destValidation.hasValue()) {
        return makeUnexpected(destValidation.error());
    }

    std::memcpy(dest, src, size);
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::memoryMove(void* dest, const void* src, size_t size) {
    if (!dest || !src) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    auto destValidation = validatePointer(dest);
    if (!destValidation.hasValue()) {
        return makeUnexpected(destValidation.error());
    }

    std::memmove(dest, src, size);
    return Expected<void, MemoryError>();
}

Expected<bool, MemoryError> MemoryManager::memoryCompare(const void* ptr1, const void* ptr2, size_t size) {
    if (!ptr1 || !ptr2) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    return std::memcmp(ptr1, ptr2, size) == 0;
}

Expected<void, MemoryError> MemoryManager::garbageCollect() {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return makeUnexpected(MemoryError::InitializationFailed);
    }

    size_t freedBytes = 0;
    auto maxAge = std::chrono::seconds(300); // 5 minutes

    auto cleanupResult = cleanupUnusedBlocks(maxAge);
    if (cleanupResult.hasValue()) {
        freedBytes = cleanupResult.value();
    }

    // Compact memory
    compactMemory();

    Logger::instance().info("Garbage collection completed, freed {} bytes", freedBytes);
    emit garbageCollectionCompleted(freedBytes);
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::compactMemory() {
    size_t compactedBytes = 0;

    // Defragment all pools
    for (auto& [type, pool] : d->pools) {
        auto result = defragmentPool(type);
        if (result.hasValue()) {
            compactedBytes += pool->totalSize - pool->usedSize;
        }
    }

    Logger::instance().info("Memory compaction completed, compacted {} bytes", compactedBytes);
    emit memoryCompactionCompleted(compactedBytes);
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::defragmentPool(MemoryPoolType type) {
    auto poolResult = findPool(type);
    if (!poolResult.hasValue()) {
        return makeUnexpected(poolResult.error());
    }

    auto pool = poolResult.value();
    
    // Simple defragmentation - move active blocks to the beginning
    std::vector<std::pair<void*, std::unique_ptr<MemoryBlock>>> activeBlocks;
    
    for (auto& [ptr, block] : pool->blocks) {
        if (block->isActive) {
            activeBlocks.emplace_back(ptr, std::move(block));
        }
    }

    pool->blocks.clear();
    
    // Reinsert active blocks
    for (auto& [ptr, block] : activeBlocks) {
        pool->blocks[ptr] = std::move(block);
    }

    Logger::instance().info("Defragmented memory pool type {}", static_cast<int>(type));
    return Expected<void, MemoryError>();
}

Expected<size_t, MemoryError> MemoryManager::cleanupUnusedBlocks(std::chrono::seconds maxAge) {
    size_t freedBytes = 0;
    auto now = std::chrono::steady_clock::now();

    for (auto& [type, pool] : d->pools) {
        std::vector<void*> blocksToRemove;
        
        for (auto& [ptr, block] : pool->blocks) {
            if (block->isActive && (now - block->allocatedAt) > maxAge) {
                // This is a simplified check - in a real implementation,
                // we'd need to track actual usage
                blocksToRemove.push_back(ptr);
            }
        }

        for (void* ptr : blocksToRemove) {
            auto blockIt = pool->blocks.find(ptr);
            if (blockIt != pool->blocks.end()) {
                freedBytes += blockIt->second->size;
                std::free(blockIt->second->ptr);
                pool->blocks.erase(blockIt);
                d->pointerToPool.erase(ptr);
                emit memoryFreed(blockIt->second->size, ptr);
            }
        }
    }

    return freedBytes;
}

Expected<MemoryStats, MemoryError> MemoryManager::getStats() const {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return makeUnexpected(MemoryError::InitializationFailed);
    }

    return d->stats;
}

Expected<MemoryStats, MemoryError> MemoryManager::getPoolStats(MemoryPoolType type) const {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return makeUnexpected(MemoryError::InitializationFailed);
    }

    auto it = d->pools.find(type);
    if (it == d->pools.end()) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    const auto& pool = it->second;
    
    MemoryStats stats;
    stats.totalAllocated = pool->totalSize;
    stats.currentUsage = pool->usedSize;
    stats.activeBlocks = pool->blockCount;
    stats.poolCount = 1;
    // Calculate other stats based on pool data
    
    return stats;
}

Expected<std::vector<MemoryBlock>, MemoryError> MemoryManager::getActiveBlocks() const {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return makeUnexpected(MemoryError::InitializationFailed);
    }

    std::vector<MemoryBlock> blocks;
    
    for (const auto& [type, pool] : d->pools) {
        for (const auto& [ptr, block] : pool->blocks) {
            if (block->isActive) {
                blocks.push_back(*block);
            }
        }
    }

    return blocks;
}

Expected<std::vector<MemoryBlock>, MemoryError> MemoryManager::getPoolBlocks(MemoryPoolType type) const {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return makeUnexpected(MemoryError::InitializationFailed);
    }

    auto it = d->pools.find(type);
    if (it == d->pools.end()) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    std::vector<MemoryBlock> blocks;
    
    for (const auto& [ptr, block] : it->second->blocks) {
        if (block->isActive) {
            blocks.push_back(*block);
        }
    }

    return blocks;
}

Expected<void, MemoryError> MemoryManager::setMemoryLimit(size_t limit) {
    QMutexLocker locker(&d->mutex);

    if (limit == 0) {
        return makeUnexpected(MemoryError::InvalidSize);
    }

    if (limit < d->currentUsage) {
        return makeUnexpected(MemoryError::InvalidSize);
    }

    d->totalMemoryLimit = limit;
    d->stats.totalAllocated = limit;
    
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::setPoolLimit(MemoryPoolType type, size_t limit) {
    auto poolResult = findPool(type);
    if (!poolResult.hasValue()) {
        return makeUnexpected(poolResult.error());
    }

    return resizePool(type, limit);
}

Expected<void, MemoryError> MemoryManager::setGarbageCollectionInterval(int intervalMs) {
    QMutexLocker locker(&d->mutex);

    if (intervalMs <= 0) {
        return makeUnexpected(MemoryError::InvalidSize);
    }

    d->garbageCollectionInterval = intervalMs;
    if (d->gcTimer) {
        d->gcTimer->setInterval(intervalMs);
    }

    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::setMemoryPressureThreshold(double threshold) {
    QMutexLocker locker(&d->mutex);

    if (threshold <= 0.0 || threshold > 1.0) {
        return makeUnexpected(MemoryError::InvalidSize);
    }

    d->memoryPressureThreshold = threshold;
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::setDebugMode(bool enabled) {
    QMutexLocker locker(&d->mutex);
    d->debugMode = enabled;
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::validateMemory() {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return makeUnexpected(MemoryError::InitializationFailed);
    }

    // Validate all pools
    for (const auto& [type, pool] : d->pools) {
        auto poolValidation = validatePool(pool.get());
        if (!poolValidation.hasValue()) {
            return poolValidation;
        }

        // Validate all blocks in the pool
        for (const auto& [ptr, block] : pool->blocks) {
            if (block->isActive) {
                auto checksumValid = verifyChecksum(*block);
                if (!checksumValid.hasValue() || !checksumValid.value()) {
                    Logger::instance().error("Memory corruption detected in block at {}", ptr);
                    return makeUnexpected(MemoryError::InvalidPointer);
                }
            }
        }
    }

    Logger::instance().info("Memory validation completed successfully");
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::detectLeaks() {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return makeUnexpected(MemoryError::InitializationFailed);
    }

    std::vector<MemoryBlock> leakedBlocks;
    auto now = std::chrono::steady_clock::now();
    auto leakThreshold = std::chrono::minutes(30); // Consider blocks older than 30 minutes as potential leaks

    for (const auto& [type, pool] : d->pools) {
        for (const auto& [ptr, block] : pool->blocks) {
            if (block->isActive && (now - block->allocatedAt) > leakThreshold) {
                leakedBlocks.push_back(*block);
            }
        }
    }

    if (!leakedBlocks.empty()) {
        size_t totalLeakedSize = 0;
        for (const auto& block : leakedBlocks) {
            totalLeakedSize += block.size;
            emit memoryLeakDetected(block.size, QString::fromStdString(block.allocatedBy));
        }

        Logger::instance().warn("Detected {} potential memory leaks, total size: {} bytes", 
                                 leakedBlocks.size(), totalLeakedSize);

        if (d->leakCallback) {
            d->leakCallback(leakedBlocks);
        }
    }

    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::dumpMemoryMap(const QString& filePath) {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return makeUnexpected(MemoryError::InitializationFailed);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return makeUnexpected(MemoryError::AllocationFailed);
    }

    QTextStream stream(&file);
    stream << "Memory Map Dump\n";
    stream << "===============\n\n";

    stream << "Total Memory Limit: " << d->totalMemoryLimit << " bytes\n";
    stream << "Current Usage: " << d->currentUsage << " bytes\n";
    stream << "Peak Usage: " << d->peakUsage << " bytes\n";
    stream << "Active Pools: " << d->pools.size() << "\n\n";

    for (const auto& [type, pool] : d->pools) {
        stream << "Pool Type: " << static_cast<int>(type) << "\n";
        stream << "  Total Size: " << pool->totalSize << " bytes\n";
        stream << "  Used Size: " << pool->usedSize << " bytes\n";
        stream << "  Available Size: " << pool->availableSize << " bytes\n";
        stream << "  Block Count: " << pool->blockCount << "\n";
        stream << "  Active Blocks:\n";

        for (const auto& [ptr, block] : pool->blocks) {
            if (block->isActive) {
                stream << "    " << ptr << " - " << block->size << " bytes"
                       << " (allocated by: " << QString::fromStdString(block->allocatedBy) << ")\n";
            }
        }
        stream << "\n";
    }

    Logger::instance().info("Memory map dumped to {}", filePath.toStdString());
    return Expected<void, MemoryError>();
}

Expected<double, MemoryError> MemoryManager::getFragmentationRatio() const {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return makeUnexpected(MemoryError::InitializationFailed);
    }

    return d->stats.fragmentationRatio;
}

Expected<size_t, MemoryError> MemoryManager::getLargestFreeBlock() const {
    QMutexLocker locker(&d->mutex);

    if (!d->initialized) {
        return makeUnexpected(MemoryError::InitializationFailed);
    }

    return d->stats.largestFreeBlock;
}

void MemoryManager::setMemoryPressureCallback(MemoryPressureCallback callback) {
    QMutexLocker locker(&d->mutex);
    d->pressureCallback = callback;
}

void MemoryManager::setOutOfMemoryCallback(OutOfMemoryCallback callback) {
    QMutexLocker locker(&d->mutex);
    d->oomCallback = callback;
}

void MemoryManager::setLeakDetectionCallback(LeakDetectionCallback callback) {
    QMutexLocker locker(&d->mutex);
    d->leakCallback = callback;
}

void MemoryManager::performGarbageCollection() {
    garbageCollect();
}

void MemoryManager::checkMemoryPressure() {
    updatePressure();
}

void MemoryManager::performLeakDetection() {
    detectLeaks();
}

Expected<void*, MemoryError> MemoryManager::internalAllocate(size_t size, size_t alignment, MemoryPoolType poolType, const std::string& allocatedBy) {
    if (d->currentUsage + size > d->totalMemoryLimit) {
        if (d->oomCallback) {
            d->oomCallback(size);
        }
        emit outOfMemory(size);
        return makeUnexpected(MemoryError::OutOfMemory);
    }

    auto poolResult = findPool(poolType);
    if (!poolResult.hasValue()) {
        return makeUnexpected(poolResult.error());
    }

    auto pool = poolResult.value();
    
    if (pool->usedSize + size > pool->totalSize) {
        return makeUnexpected(MemoryError::PoolExhausted);
    }

    // Allocate memory
    void* ptr = nullptr;
    if (alignment > 1) {
        ptr = std::aligned_alloc(alignment, size);
    } else {
        ptr = std::malloc(size);
    }

    if (!ptr) {
        return makeUnexpected(MemoryError::AllocationFailed);
    }

    // Add block to pool
    auto addResult = addBlock(pool, ptr, size, alignment, allocatedBy);
    if (!addResult.hasValue()) {
        std::free(ptr);
        return makeUnexpected(addResult.error());
    }

    // Update stats
    updateStats(pool, size, true);
    
    d->currentUsage += size;
    if (d->currentUsage > d->peakUsage) {
        d->peakUsage = d->currentUsage;
    }

    d->pointerToPool[ptr] = poolType;

    if (d->debugMode) {
        logAllocation(ptr, size, allocatedBy);
    }

    emit memoryAllocated(size, ptr);
    return ptr;
}

Expected<void, MemoryError> MemoryManager::internalDeallocate(void* ptr) {
    auto poolResult = findPoolForPointer(ptr);
    if (!poolResult.hasValue()) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    auto pool = poolResult.value();
    auto blockIt = pool->blocks.find(ptr);
    if (blockIt == pool->blocks.end()) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    auto& block = blockIt->second;
    if (!block->isActive) {
        return makeUnexpected(MemoryError::DoubleFreePrevention);
    }

    size_t size = block->size;
    
    // Free memory
    std::free(ptr);
    
    // Remove block from pool
    removeBlock(pool, ptr);
    
    // Update stats
    updateStats(pool, size, false);
    
    d->currentUsage -= size;
    d->pointerToPool.erase(ptr);

    if (d->debugMode) {
        logDeallocation(ptr, size);
    }

    emit memoryFreed(size, ptr);
    return Expected<void, MemoryError>();
}

Expected<MemoryPool*, MemoryError> MemoryManager::findPool(MemoryPoolType type) {
    auto it = d->pools.find(type);
    if (it == d->pools.end()) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }
    return it->second.get();
}

Expected<MemoryPool*, MemoryError> MemoryManager::findPoolForPointer(void* ptr) {
    auto it = d->pointerToPool.find(ptr);
    if (it == d->pointerToPool.end()) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }
    return findPool(it->second);
}

Expected<void, MemoryError> MemoryManager::addBlock(MemoryPool* pool, void* ptr, size_t size, size_t alignment, const std::string& allocatedBy) {
    auto block = std::make_unique<MemoryBlock>();
    block->ptr = ptr;
    block->size = size;
    block->alignment = alignment;
    block->poolType = pool->type;
    block->allocatedAt = std::chrono::steady_clock::now();
    block->allocatedBy = allocatedBy;
    block->isActive = true;
    block->requestedSize = size;

    // Calculate checksum
    auto checksum = calculateChecksum(ptr, size);
    if (checksum.hasValue()) {
        block->checksum = checksum.value();
    }

    pool->blocks[ptr] = std::move(block);
    pool->blockCount++;
    pool->usedSize += size;
    pool->availableSize -= size;

    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::removeBlock(MemoryPool* pool, void* ptr) {
    auto it = pool->blocks.find(ptr);
    if (it == pool->blocks.end()) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    size_t size = it->second->size;
    pool->blocks.erase(it);
    pool->blockCount--;
    pool->usedSize -= size;
    pool->availableSize += size;

    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::updateStats(MemoryPool* pool, size_t size, bool isAllocation) {
    if (isAllocation) {
        d->stats.totalAllocated += size;
        d->stats.allocationCount++;
        d->stats.activeBlocks++;
    } else {
        d->stats.totalFreed += size;
        d->stats.freeCount++;
        d->stats.activeBlocks--;
    }

    d->stats.currentUsage = d->currentUsage;
    if (d->currentUsage > d->stats.peakUsage) {
        d->stats.peakUsage = d->currentUsage;
    }

    // Update fragmentation ratio
    calculateFragmentation();
    
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::validatePointer(void* ptr) const {
    if (!ptr) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    auto it = d->pointerToPool.find(ptr);
    if (it == d->pointerToPool.end()) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::validateSize(size_t size) const {
    if (size == 0) {
        return makeUnexpected(MemoryError::InvalidSize);
    }

    if (size > d->totalMemoryLimit) {
        return makeUnexpected(MemoryError::InvalidSize);
    }

    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::validateAlignment(size_t alignment) const {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return makeUnexpected(MemoryError::InvalidAlignment);
    }

    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::validatePool(const MemoryPool* pool) const {
    if (!pool) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    if (!pool->isActive) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    if (pool->usedSize > pool->totalSize) {
        return makeUnexpected(MemoryError::PoolExhausted);
    }

    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::calculateFragmentation() {
    size_t totalFree = 0;
    size_t largestFree = 0;
    size_t smallestFree = SIZE_MAX;

    for (const auto& [type, pool] : d->pools) {
        size_t poolFree = pool->availableSize;
        totalFree += poolFree;
        
        if (poolFree > largestFree) {
            largestFree = poolFree;
        }
        
        if (poolFree < smallestFree && poolFree > 0) {
            smallestFree = poolFree;
        }
    }

    if (totalFree > 0) {
        d->stats.fragmentationRatio = 1.0 - (static_cast<double>(largestFree) / totalFree);
    } else {
        d->stats.fragmentationRatio = 0.0;
    }

    d->stats.largestFreeBlock = largestFree;
    d->stats.smallestFreeBlock = (smallestFree == SIZE_MAX) ? 0 : smallestFree;

    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::updatePressure() {
    double pressure = static_cast<double>(d->currentUsage) / d->totalMemoryLimit;
    
    if (pressure > d->memoryPressureThreshold) {
        if (d->pressureCallback) {
            d->pressureCallback(pressure);
        }
        emit memoryPressure(pressure);
    }

    return Expected<void, MemoryError>();
}

Expected<size_t, MemoryError> MemoryManager::alignSize(size_t size, size_t alignment) const {
    return (size + alignment - 1) & ~(alignment - 1);
}

Expected<bool, MemoryError> MemoryManager::isPointerValid(void* ptr) const {
    return d->pointerToPool.find(ptr) != d->pointerToPool.end();
}

Expected<QByteArray, MemoryError> MemoryManager::calculateChecksum(void* ptr, size_t size) const {
    if (!ptr || size == 0) {
        return makeUnexpected(MemoryError::InvalidPointer);
    }

    QByteArray data(static_cast<const char*>(ptr), static_cast<qsizetype>(size));
    return QCryptographicHash::hash(data, QCryptographicHash::Md5);
}

Expected<bool, MemoryError> MemoryManager::verifyChecksum(const MemoryBlock& block) const {
    if (!block.ptr || block.size == 0) {
        return false;
    }

    auto currentChecksum = calculateChecksum(block.ptr, block.size);
    if (!currentChecksum.hasValue()) {
        return false;
    }

    return currentChecksum.value() == block.checksum;
}

Expected<void, MemoryError> MemoryManager::logAllocation(void* ptr, size_t size, const std::string& allocatedBy) {
    Logger::instance().debug("Memory allocated: {} bytes at {} by {}", size, ptr, allocatedBy);
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::logDeallocation(void* ptr, size_t size) {
    Logger::instance().debug("Memory deallocated: {} bytes at {}", size, ptr);
    return Expected<void, MemoryError>();
}

Expected<void, MemoryError> MemoryManager::logMemoryStatus() {
    Logger::instance().info("Memory Status - Current: {} bytes, Peak: {} bytes, Limit: {} bytes", 
                           d->currentUsage, d->peakUsage, d->totalMemoryLimit);
    return Expected<void, MemoryError>();
}

} // namespace Murmur

