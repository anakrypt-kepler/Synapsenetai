#include "utils/utils.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <memory>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>

namespace synapse {
namespace utils {

class SecureMemory {
private:
    void* data;
    size_t size;
    bool locked;
    bool wiped;

public:
    SecureMemory(size_t sz) : size(sz), locked(false), wiped(false) {
        data = allocateSecure(sz);
    }

    ~SecureMemory() {
        if (data && !wiped) {
            wipe();
        }
        if (data) {
            deallocateSecure(data, size);
        }
    }

    void* allocateSecure(size_t sz) {
        void* ptr = std::malloc(sz);
        if (ptr) {
            std::memset(ptr, 0, sz);
        }
        return ptr;
    }

    void deallocateSecure(void* ptr, size_t sz) {
        if (ptr) {
            std::memset(ptr, 0, sz);
            std::free(ptr);
        }
    }

    void wipe() {
        if (data && !wiped) {
            volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
            for (size_t i = 0; i < size; i++) {
                p[i] = 0;
            }
            for (size_t i = 0; i < size; i++) {
                p[i] = 0xFF;
            }
            for (size_t i = 0; i < size; i++) {
                p[i] = 0;
            }
            wiped = true;
        }
    }

    bool lock() {
        locked = true;
        return true;
    }

    bool unlock() {
        locked = false;
        return true;
    }

    void* get() { return data; }
    const void* get() const { return data; }
    size_t getSize() const { return size; }
    bool isLocked() const { return locked; }
    bool isWiped() const { return wiped; }

    SecureMemory(const SecureMemory&) = delete;
    SecureMemory& operator=(const SecureMemory&) = delete;

    SecureMemory(SecureMemory&& other) noexcept 
        : data(other.data), size(other.size), locked(other.locked), wiped(other.wiped) {
        other.data = nullptr;
        other.size = 0;
        other.locked = false;
        other.wiped = true;
    }

    SecureMemory& operator=(SecureMemory&& other) noexcept {
        if (this != &other) {
            if (data && !wiped) {
                wipe();
            }
            if (data) {
                deallocateSecure(data, size);
            }

            data = other.data;
            size = other.size;
            locked = other.locked;
            wiped = other.wiped;

            other.data = nullptr;
            other.size = 0;
            other.locked = false;
            other.wiped = true;
        }
        return *this;
    }
};

class MemoryPool {
private:
    struct Block {
        void* data;
        size_t size;
        bool inUse;
        std::chrono::steady_clock::time_point lastUsed;
    };

    std::vector<Block> blocks;
    size_t blockSize;
    size_t maxBlocks;
    size_t totalAllocated;
    size_t totalUsed;
    std::mutex mutex;

public:
    MemoryPool(size_t blkSize = 4096, size_t maxBlks = 100)
        : blockSize(blkSize), maxBlocks(maxBlks), totalAllocated(0), totalUsed(0) {}

    ~MemoryPool() {
        clear();
    }

    void* allocate(size_t size) {
        std::lock_guard<std::mutex> lock(mutex);

        if (size <= blockSize) {
            for (auto& block : blocks) {
                if (!block.inUse && block.size >= size) {
                    block.inUse = true;
                    block.lastUsed = std::chrono::steady_clock::now();
                    totalUsed += block.size;
                    return block.data;
                }
            }

            if (blocks.size() < maxBlocks) {
                Block newBlock;
                newBlock.data = std::malloc(blockSize);
                newBlock.size = blockSize;
                newBlock.inUse = true;
                newBlock.lastUsed = std::chrono::steady_clock::now();

                if (newBlock.data) {
                    blocks.push_back(newBlock);
                    totalAllocated += blockSize;
                    totalUsed += blockSize;
                    return newBlock.data;
                }
            }
        }

        void* ptr = std::malloc(size);
        if (ptr) {
            totalAllocated += size;
            totalUsed += size;
        }
        return ptr;
    }

    void deallocate(void* ptr) {
        std::lock_guard<std::mutex> lock(mutex);

        for (auto& block : blocks) {
            if (block.data == ptr) {
                block.inUse = false;
                totalUsed -= block.size;
                return;
            }
        }

        std::free(ptr);
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);

        for (auto& block : blocks) {
            if (block.data) {
                std::memset(block.data, 0, block.size);
                std::free(block.data);
            }
        }
        blocks.clear();
        totalAllocated = 0;
        totalUsed = 0;
    }

    void compact() {
        std::lock_guard<std::mutex> lock(mutex);

        auto now = std::chrono::steady_clock::now();
        blocks.erase(
            std::remove_if(blocks.begin(), blocks.end(),
                [&](const Block& b) {
                    if (!b.inUse) {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            now - b.lastUsed).count();
                        if (elapsed > 60) {
                            std::free(b.data);
                            totalAllocated -= b.size;
                            return true;
                        }
                    }
                    return false;
                }),
            blocks.end());
    }

    size_t getTotalAllocated() const { return totalAllocated; }
    size_t getTotalUsed() const { return totalUsed; }
    size_t getBlockCount() const { return blocks.size(); }
    size_t getFreeBlocks() const {
        size_t count = 0;
        for (const auto& b : blocks) {
            if (!b.inUse) count++;
        }
        return count;
    }
};

class BufferPool {
private:
    std::map<size_t, std::vector<std::vector<uint8_t>>> pools;
    std::mutex mutex;
    size_t maxBuffersPerSize;
    std::atomic<size_t> totalBuffers;
    std::atomic<size_t> totalMemory;

public:
    BufferPool(size_t maxPerSize = 10) 
        : maxBuffersPerSize(maxPerSize), totalBuffers(0), totalMemory(0) {}

    std::vector<uint8_t> acquire(size_t size) {
        std::lock_guard<std::mutex> lock(mutex);

        size_t alignedSize = alignSize(size);
        auto& pool = pools[alignedSize];

        if (!pool.empty()) {
            auto buffer = std::move(pool.back());
            pool.pop_back();
            buffer.resize(size);
            return buffer;
        }

        totalBuffers++;
        totalMemory += alignedSize;
        return std::vector<uint8_t>(size);
    }

    void release(std::vector<uint8_t>&& buffer) {
        std::lock_guard<std::mutex> lock(mutex);

        size_t alignedSize = alignSize(buffer.capacity());
        auto& pool = pools[alignedSize];

        if (pool.size() < maxBuffersPerSize) {
            std::fill(buffer.begin(), buffer.end(), 0);
            pool.push_back(std::move(buffer));
        } else {
            totalBuffers--;
            totalMemory -= alignedSize;
        }
    }

    size_t alignSize(size_t size) {
        const size_t alignment = 64;
        return ((size + alignment - 1) / alignment) * alignment;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        pools.clear();
        totalBuffers = 0;
        totalMemory = 0;
    }

    size_t getTotalBuffers() const { return totalBuffers; }
    size_t getTotalMemory() const { return totalMemory; }
};

class CacheManager {
private:
    struct CacheEntry {
        std::vector<uint8_t> data;
        std::chrono::steady_clock::time_point created;
        std::chrono::steady_clock::time_point lastAccess;
        uint64_t accessCount;
        size_t size;
        bool pinned;
    };

    std::map<std::string, CacheEntry> cache;
    size_t maxSize;
    size_t currentSize;
    std::mutex mutex;
    std::chrono::seconds defaultTTL;

public:
    CacheManager(size_t maxSz = 100 * 1024 * 1024, int ttlSeconds = 3600)
        : maxSize(maxSz), currentSize(0), defaultTTL(ttlSeconds) {}

    bool put(const std::string& key, const std::vector<uint8_t>& data, bool pin = false) {
        std::lock_guard<std::mutex> lock(mutex);

        if (data.size() > maxSize) return false;

        while (currentSize + data.size() > maxSize) {
            if (!evictOne()) break;
        }

        if (currentSize + data.size() > maxSize) return false;

        CacheEntry entry;
        entry.data = data;
        entry.created = std::chrono::steady_clock::now();
        entry.lastAccess = entry.created;
        entry.accessCount = 0;
        entry.size = data.size();
        entry.pinned = pin;

        auto it = cache.find(key);
        if (it != cache.end()) {
            currentSize -= it->second.size;
        }

        cache[key] = std::move(entry);
        currentSize += data.size();

        return true;
    }

    bool get(const std::string& key, std::vector<uint8_t>& data) {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = cache.find(key);
        if (it == cache.end()) return false;

        it->second.lastAccess = std::chrono::steady_clock::now();
        it->second.accessCount++;
        data = it->second.data;

        return true;
    }

    bool contains(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex);
        return cache.find(key) != cache.end();
    }

    void remove(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = cache.find(key);
        if (it != cache.end()) {
            currentSize -= it->second.size;
            cache.erase(it);
        }
    }

    void pin(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = cache.find(key);
        if (it != cache.end()) {
            it->second.pinned = true;
        }
    }

    void unpin(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = cache.find(key);
        if (it != cache.end()) {
            it->second.pinned = false;
        }
    }

    bool evictOne() {
        if (cache.empty()) return false;

        std::string keyToEvict;
        auto oldestAccess = std::chrono::steady_clock::now();
        uint64_t lowestCount = UINT64_MAX;

        for (const auto& pair : cache) {
            if (pair.second.pinned) continue;

            if (pair.second.accessCount < lowestCount ||
                (pair.second.accessCount == lowestCount && 
                 pair.second.lastAccess < oldestAccess)) {
                keyToEvict = pair.first;
                oldestAccess = pair.second.lastAccess;
                lowestCount = pair.second.accessCount;
            }
        }

        if (keyToEvict.empty()) return false;

        currentSize -= cache[keyToEvict].size;
        cache.erase(keyToEvict);
        return true;
    }

    void evictExpired() {
        std::lock_guard<std::mutex> lock(mutex);

        auto now = std::chrono::steady_clock::now();
        for (auto it = cache.begin(); it != cache.end();) {
            if (!it->second.pinned) {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(
                    now - it->second.created);
                if (age > defaultTTL) {
                    currentSize -= it->second.size;
                    it = cache.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        cache.clear();
        currentSize = 0;
    }

    size_t getSize() const { return currentSize; }
    size_t getMaxSize() const { return maxSize; }
    size_t getEntryCount() const { return cache.size(); }
    double getUsagePercent() const { 
        return maxSize > 0 ? (100.0 * currentSize / maxSize) : 0.0; 
    }
};

class ArenaAllocator {
private:
    struct Arena {
        std::vector<uint8_t> data;
        size_t used;
    };

    std::vector<Arena> arenas;
    size_t arenaSize;
    size_t currentArena;
    std::mutex mutex;

public:
    ArenaAllocator(size_t arenaSz = 1024 * 1024) 
        : arenaSize(arenaSz), currentArena(0) {
        addArena();
    }

    void addArena() {
        Arena arena;
        arena.data.resize(arenaSize);
        arena.used = 0;
        arenas.push_back(std::move(arena));
    }

    void* allocate(size_t size, size_t alignment = 8) {
        std::lock_guard<std::mutex> lock(mutex);

        size_t alignedSize = ((size + alignment - 1) / alignment) * alignment;

        if (alignedSize > arenaSize) {
            return nullptr;
        }

        Arena& arena = arenas[currentArena];
        size_t alignedOffset = ((arena.used + alignment - 1) / alignment) * alignment;

        if (alignedOffset + alignedSize > arenaSize) {
            currentArena++;
            if (currentArena >= arenas.size()) {
                addArena();
            }
            return allocate(size, alignment);
        }

        void* ptr = arena.data.data() + alignedOffset;
        arena.used = alignedOffset + alignedSize;

        return ptr;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& arena : arenas) {
            std::fill(arena.data.begin(), arena.data.end(), 0);
            arena.used = 0;
        }
        currentArena = 0;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        arenas.clear();
        currentArena = 0;
        addArena();
    }

    size_t getTotalAllocated() const {
        size_t total = 0;
        for (const auto& arena : arenas) {
            total += arena.used;
        }
        return total;
    }

    size_t getTotalCapacity() const {
        return arenas.size() * arenaSize;
    }

    size_t getArenaCount() const {
        return arenas.size();
    }
};

class MemoryTracker {
private:
    struct Allocation {
        void* ptr;
        size_t size;
        std::string tag;
        std::chrono::steady_clock::time_point time;
    };

    std::map<void*, Allocation> allocations;
    std::map<std::string, size_t> taggedSizes;
    std::atomic<size_t> totalAllocated;
    std::atomic<size_t> peakAllocated;
    std::atomic<uint64_t> allocationCount;
    std::mutex mutex;

public:
    MemoryTracker() : totalAllocated(0), peakAllocated(0), allocationCount(0) {}

    void trackAllocation(void* ptr, size_t size, const std::string& tag = "") {
        std::lock_guard<std::mutex> lock(mutex);

        Allocation alloc;
        alloc.ptr = ptr;
        alloc.size = size;
        alloc.tag = tag;
        alloc.time = std::chrono::steady_clock::now();

        allocations[ptr] = alloc;
        totalAllocated += size;
        allocationCount++;

        if (totalAllocated > peakAllocated) {
            peakAllocated = totalAllocated.load();
        }

        if (!tag.empty()) {
            taggedSizes[tag] += size;
        }
    }

    void trackDeallocation(void* ptr) {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = allocations.find(ptr);
        if (it != allocations.end()) {
            totalAllocated -= it->second.size;
            if (!it->second.tag.empty()) {
                taggedSizes[it->second.tag] -= it->second.size;
            }
            allocations.erase(it);
        }
    }

    size_t getTotalAllocated() const { return totalAllocated; }
    size_t getPeakAllocated() const { return peakAllocated; }
    uint64_t getAllocationCount() const { return allocationCount; }
    size_t getActiveAllocations() const { return allocations.size(); }

    size_t getTaggedSize(const std::string& tag) const {
        auto it = taggedSizes.find(tag);
        return (it != taggedSizes.end()) ? it->second : 0;
    }

    std::vector<std::pair<std::string, size_t>> getTaggedSizes() const {
        std::vector<std::pair<std::string, size_t>> result(taggedSizes.begin(), taggedSizes.end());
        std::sort(result.begin(), result.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        return result;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex);
        allocations.clear();
        taggedSizes.clear();
        totalAllocated = 0;
        peakAllocated = 0;
        allocationCount = 0;
    }
};

}
}
