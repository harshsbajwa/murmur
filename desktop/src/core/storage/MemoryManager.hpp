#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <memory>
#include <functional>
#include <unordered_map>
#include <chrono>

#include "core/common/Expected.hpp"
#include "core/common/Logger.hpp"

namespace Murmur {

enum class MemoryError {
    InitializationFailed,
    AllocationFailed,
    InvalidSize,
    InvalidAlignment,
    OutOfMemory,
    PoolExhausted,
    FragmentationError,
    InvalidPointer,
    DoubleFreePrevention,
    MemoryLeakDetected
};

enum class MemoryPoolType {
    General,
    Video,
    Audio,
    Transcription,
    Torrent,
    Temporary,
    Large
};

struct MemoryBlock {
    void* ptr;
    size_t size;
    size_t alignment;
    MemoryPoolType poolType;
    std::chrono::steady_clock::time_point allocatedAt;
    std::string allocatedBy;
    bool isActive;
    size_t requestedSize;
    QByteArray checksum;
};

struct MemoryPool {
    MemoryPoolType type;
    size_t totalSize;
    size_t usedSize;
    size_t availableSize;
    size_t blockCount;
    size_t maxBlockSize;
    void* basePtr;
    std::unordered_map<void*, std::unique_ptr<MemoryBlock>> blocks;
    bool isActive;
    size_t alignment;
    std::chrono::steady_clock::time_point createdAt;
};

struct MemoryStats {
    size_t totalAllocated;
    size_t totalFreed;
    size_t currentUsage;
    size_t peakUsage;
    size_t allocationCount;
    size_t freeCount;
    size_t activeBlocks;
    size_t poolCount;
    double fragmentationRatio;
    size_t largestFreeBlock;
    size_t smallestFreeBlock;
    std::chrono::steady_clock::time_point lastReset;
};

class MemoryManager : public QObject {
    Q_OBJECT

public:
    explicit MemoryManager(QObject* parent = nullptr);
    ~MemoryManager() override;

    // Initialization
    Expected<void, MemoryError> initialize(size_t totalMemoryLimit = 1024 * 1024 * 1024); // 1GB default
    Expected<void, MemoryError> shutdown();
    bool isInitialized() const;

    // Memory allocation
    Expected<void*, MemoryError> allocate(size_t size, size_t alignment = 16, MemoryPoolType poolType = MemoryPoolType::General, const std::string& allocatedBy = "");
    Expected<void, MemoryError> deallocate(void* ptr);
    Expected<void*, MemoryError> reallocate(void* ptr, size_t newSize, size_t alignment = 16);
    Expected<void*, MemoryError> alignedAllocate(size_t size, size_t alignment, MemoryPoolType poolType = MemoryPoolType::General);

    // Pool management
    Expected<void, MemoryError> createPool(MemoryPoolType type, size_t size, size_t alignment = 16);
    Expected<void, MemoryError> destroyPool(MemoryPoolType type);
    Expected<void, MemoryError> resizePool(MemoryPoolType type, size_t newSize);
    Expected<void, MemoryError> clearPool(MemoryPoolType type);

    // Memory operations
    Expected<void, MemoryError> memorySet(void* ptr, int value, size_t size);
    Expected<void, MemoryError> memoryCopy(void* dest, const void* src, size_t size);
    Expected<void, MemoryError> memoryMove(void* dest, const void* src, size_t size);
    Expected<bool, MemoryError> memoryCompare(const void* ptr1, const void* ptr2, size_t size);

    // Garbage collection
    Expected<void, MemoryError> garbageCollect();
    Expected<void, MemoryError> compactMemory();
    Expected<void, MemoryError> defragmentPool(MemoryPoolType type);
    Expected<size_t, MemoryError> cleanupUnusedBlocks(std::chrono::seconds maxAge);

    // Memory tracking
    Expected<MemoryStats, MemoryError> getStats() const;
    Expected<MemoryStats, MemoryError> getPoolStats(MemoryPoolType type) const;
    Expected<std::vector<MemoryBlock>, MemoryError> getActiveBlocks() const;
    Expected<std::vector<MemoryBlock>, MemoryError> getPoolBlocks(MemoryPoolType type) const;

    // Configuration
    Expected<void, MemoryError> setMemoryLimit(size_t limit);
    Expected<void, MemoryError> setPoolLimit(MemoryPoolType type, size_t limit);
    Expected<void, MemoryError> setGarbageCollectionInterval(int intervalMs);
    Expected<void, MemoryError> setMemoryPressureThreshold(double threshold);
    Expected<void, MemoryError> setDebugMode(bool enabled);

    // Diagnostics
    Expected<void, MemoryError> validateMemory();
    Expected<void, MemoryError> detectLeaks();
    Expected<void, MemoryError> dumpMemoryMap(const QString& filePath);
    Expected<double, MemoryError> getFragmentationRatio() const;
    Expected<size_t, MemoryError> getLargestFreeBlock() const;

    // Callbacks
    using MemoryPressureCallback = std::function<void(double pressure)>;
    using OutOfMemoryCallback = std::function<void(size_t requestedSize)>;
    using LeakDetectionCallback = std::function<void(const std::vector<MemoryBlock>&)>;

    void setMemoryPressureCallback(MemoryPressureCallback callback);
    void setOutOfMemoryCallback(OutOfMemoryCallback callback);
    void setLeakDetectionCallback(LeakDetectionCallback callback);

signals:
    void memoryAllocated(size_t size, void* ptr);
    void memoryFreed(size_t size, void* ptr);
    void memoryPressure(double pressure);
    void outOfMemory(size_t requestedSize);
    void memoryLeakDetected(size_t leakSize, const QString& location);
    void garbageCollectionCompleted(size_t freedBytes);
    void memoryCompactionCompleted(size_t compactedBytes);
    void poolCreated(MemoryPoolType type, size_t size);
    void poolDestroyed(MemoryPoolType type);

private slots:
    void performGarbageCollection();
    void checkMemoryPressure();
    void performLeakDetection();

private:
    class MemoryManagerPrivate;
    std::unique_ptr<MemoryManagerPrivate> d;

    // Internal allocation
    Expected<void*, MemoryError> internalAllocate(size_t size, size_t alignment, MemoryPoolType poolType, const std::string& allocatedBy);
    Expected<void, MemoryError> internalDeallocate(void* ptr);
    Expected<MemoryPool*, MemoryError> findPool(MemoryPoolType type);
    Expected<MemoryPool*, MemoryError> findPoolForPointer(void* ptr);

    // Memory management
    Expected<void, MemoryError> addBlock(MemoryPool* pool, void* ptr, size_t size, size_t alignment, const std::string& allocatedBy);
    Expected<void, MemoryError> removeBlock(MemoryPool* pool, void* ptr);
    Expected<void, MemoryError> updateStats(MemoryPool* pool, size_t size, bool isAllocation);

    // Validation
    Expected<void, MemoryError> validatePointer(void* ptr) const;
    Expected<void, MemoryError> validateSize(size_t size) const;
    Expected<void, MemoryError> validateAlignment(size_t alignment) const;
    Expected<void, MemoryError> validatePool(const MemoryPool* pool) const;

    // Utility
    Expected<void, MemoryError> calculateFragmentation();
    Expected<void, MemoryError> updatePressure();
    Expected<size_t, MemoryError> alignSize(size_t size, size_t alignment) const;
    Expected<bool, MemoryError> isPointerValid(void* ptr) const;
    Expected<QByteArray, MemoryError> calculateChecksum(void* ptr, size_t size) const;
    Expected<bool, MemoryError> verifyChecksum(const MemoryBlock& block) const;

    // Debug
    Expected<void, MemoryError> logAllocation(void* ptr, size_t size, const std::string& allocatedBy);
    Expected<void, MemoryError> logDeallocation(void* ptr, size_t size);
    Expected<void, MemoryError> logMemoryStatus();
};

} // namespace Murmur