#ifndef CLOCK_SWEEP_HPP
#define CLOCK_SWEEP_HPP

#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>
#include <stdexcept>
#include <iomanip>

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class ClockSweep {
public:
    // Core block representing a single slot in our buffer pool
    struct CacheBlock {
        Key pageKey;
        Value data;
        bool secondChance = false; // Replaces referenceBit
        bool isEmpty = true;        // Inverted: Replaces occupied (empty by default)
        bool dirtyFlag = false;     // Replaces isDirty
        int pinRefs = 0;           // Replaces pinCount
    };

    // Constructor: Pre-allocates block storage and spawns background aging worker
    ClockSweep(size_t maxCapacity, unsigned int maintenanceIntervalMs = 2000, bool verbose = false)
        : maxFrames(maxCapacity),
          pointerIndex(0),
          stopJanitor(false),
          janitorIntervalMs(maintenanceIntervalMs),
          verboseLogging(verbose),
          statsHits(0),
          statsMisses(0),
          statsEvictions(0),
          statsWritebacks(0) {
        
        if (maxFrames == 0) {
            throw std::invalid_argument("Capacity must be at least 1");
        }
        
        // Allocate space for slots
        pool.resize(maxFrames);
        
        // Initialize background cleanup thread (Janitor)
        janitorThread = std::thread(&ClockSweep::runJanitorCycle, this);
        
        if (verboseLogging) {
            std::cout << "[Janitor Services] Initialized buffer pool with size: " 
                      << maxFrames << ", Decay Cycle: " << maintenanceIntervalMs << "ms" << std::endl;
        }
    }

    // Destructor: Terminates background janitor thread and releases buffer resources
    ~ClockSweep() {
        {
            std::lock_guard<std::mutex> guard(lock);
            stopJanitor = true;
        }
        cv.notify_all();
        
        if (janitorThread.joinable()) {
            janitorThread.join();
        }
        
        if (verboseLogging) {
            std::cout << "[Janitor Services] Shutdown successful. Cache cleared." << std::endl;
        }
    }

    // Retrieve a page from the buffer
    std::optional<Value> get(const Key& key) {
        std::lock_guard<std::mutex> guard(lock);
        
        auto match = mapTable.find(key);
        if (match != mapTable.end()) {
            size_t slotIdx = match->second;
            pool[slotIdx].secondChance = true; // Give a second chance (referenced)
            statsHits++;
            
            if (verboseLogging) {
                std::cout << "[Read Access] Hit on key '" << key << "' at index " << slotIdx << std::endl;
            }
            return pool[slotIdx].data;
        }
        
        statsMisses++;
        if (verboseLogging) {
            std::cout << "[Read Access] Miss on key '" << key << "' (Not found)" << std::endl;
        }
        return std::nullopt;
    }

    // Fetch page using standard boolean reference return
    bool get(const Key& key, Value& valueOut) {
        std::optional<Value> result = get(key);
        if (result.has_value()) {
            valueOut = result.value();
            return true;
        }
        return false;
    }

    // Write or update an entry in the buffer pool
    void put(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> guard(lock);
        
        auto match = mapTable.find(key);
        if (match != mapTable.end()) {
            // Entry already exists, overwrite data and mark referenced
            size_t slotIdx = match->second;
            
            if (!(pool[slotIdx].data == value)) {
                pool[slotIdx].data = value;
                pool[slotIdx].dirtyFlag = true; // Modified
            }
            pool[slotIdx].secondChance = true;
            
            if (verboseLogging) {
                std::cout << "[Write Access] Overwrote existing key '" << key << "' at index " << slotIdx << std::endl;
            }
            return;
        }
        
        // If pool is at maximum capacity, evict a page using the clock hand sweep
        if (mapTable.size() >= maxFrames) {
            try {
                size_t victimIdx = selectVictimSlot();
                CacheBlock& victim = pool[victimIdx];
                Key oldKey = victim.pageKey;
                
                // Write back page if it was modified
                if (victim.dirtyFlag) {
                    statsWritebacks++;
                    if (verboseLogging) {
                        std::cout << "[Storage Flush] Writing dirty page '" << oldKey 
                                  << "' to persistent storage (data: " << victim.data << ")." << std::endl;
                    }
                }
                
                // Remove victim mapping
                mapTable.erase(oldKey);
                statsEvictions++;
                
                if (verboseLogging) {
                    std::cout << "[Eviction Agent] Evicting key '" << oldKey 
                              << "' from index " << victimIdx << " to make space for '" << key << "'." << std::endl;
                }
                
                // Insert new data at the victim's place
                victim.pageKey = key;
                victim.data = value;
                victim.secondChance = false; // Starts as 0 per Section 11 workflow
                victim.isEmpty = false;
                victim.dirtyFlag = false;
                victim.pinRefs = 0;
                
                mapTable[key] = victimIdx;
                
            } catch (const std::runtime_error& err) {
                std::cerr << "[Storage Crash] " << err.what() << " Failed insertion of '" << key << "'." << std::endl;
                throw;
            }
        } else {
            // Buffer pool is not full, find the first unused empty slot
            for (size_t i = 0; i < maxFrames; ++i) {
                if (pool[i].isEmpty) {
                    pool[i].pageKey = key;
                    pool[i].data = value;
                    pool[i].secondChance = false; // Starts as 0 per Section 11 workflow
                    pool[i].isEmpty = false;
                    pool[i].dirtyFlag = false;
                    pool[i].pinRefs = 0;
                    
                    mapTable[key] = i;
                    
                    if (verboseLogging) {
                        std::cout << "[Write Access] Placed key '" << key << "' into empty slot " << i << std::endl;
                    }
                    break;
                }
            }
        }
    }

    // Pin a buffer block (disallows eviction)
    bool pin(const Key& key) {
        std::lock_guard<std::mutex> guard(lock);
        auto match = mapTable.find(key);
        if (match != mapTable.end()) {
            size_t idx = match->second;
            pool[idx].pinRefs++;
            if (verboseLogging) {
                std::cout << "[Lock Service] Key '" << key << "' pinned (Refs: " 
                          << pool[idx].pinRefs << ")" << std::endl;
            }
            return true;
        }
        return false;
    }

    // Unpin a buffer block (permits eviction once count returns to 0)
    bool unpin(const Key& key) {
        std::lock_guard<std::mutex> guard(lock);
        auto match = mapTable.find(key);
        if (match != mapTable.end()) {
            size_t idx = match->second;
            if (pool[idx].pinRefs > 0) {
                pool[idx].pinRefs--;
                if (verboseLogging) {
                    std::cout << "[Lock Service] Key '" << key << "' unpinned (Refs: " 
                              << pool[idx].pinRefs << ")" << std::endl;
                }
                return true;
            }
        }
        return false;
    }

    // Flag a page as modified manually
    bool markDirty(const Key& key) {
        std::lock_guard<std::mutex> guard(lock);
        auto match = mapTable.find(key);
        if (match != mapTable.end()) {
            size_t idx = match->second;
            pool[idx].dirtyFlag = true;
            if (verboseLogging) {
                std::cout << "[Flag Service] Marked key '" << key << "' as modified (Dirty)." << std::endl;
            }
            return true;
        }
        return false;
    }

    // Set debug mode
    void setDebugMode(bool debug) {
        std::lock_guard<std::mutex> guard(lock);
        verboseLogging = debug;
    }

    // Status readers
    size_t getHits() const { return statsHits; }
    size_t getMisses() const { return statsMisses; }
    size_t getEvictions() const { return statsEvictions; }
    size_t getDirtyWritebacks() const { return statsWritebacks; }
    
    double getHitRate() const {
        size_t total = statsHits + statsMisses;
        return total == 0 ? 0.0 : (static_cast<double>(statsHits) / total) * 100.0;
    }

    void resetStats() {
        std::lock_guard<std::mutex> guard(lock);
        statsHits = 0;
        statsMisses = 0;
        statsEvictions = 0;
        statsWritebacks = 0;
    }

    // Render the state of our cache in a beautiful table
    void printCacheState() const {
        std::lock_guard<std::mutex> guard(lock);
        std::cout << "\n" << std::string(62, '#') << "\n";
        std::cout << " BUFFER POOL REPLACER STATUS (Elements: " << mapTable.size() << " / " << maxFrames << ")\n";
        std::cout << std::string(62, '-') << "\n";
        std::cout << "  Slot  | State |  Key  |  Value  | Second Chance | Dirty | Pinned\n";
        std::cout << std::string(62, '-') << "\n";

        for (size_t i = 0; i < maxFrames; ++i) {
            const CacheBlock& b = pool[i];
            bool hasHand = (i == pointerIndex);
            
            std::cout << "   " << std::setw(3) << i << "  | ";
            if (b.isEmpty == false) {
                std::cout << "Active | " 
                          << std::setw(5) << b.pageKey << " | " 
                          << std::setw(7) << b.data << " | "
                          << "      " << b.secondChance << "       | "
                          << "  " << b.dirtyFlag << "   | "
                          << "   " << b.pinRefs;
            } else {
                std::cout << "Free   |   -   |    -    |       -       |   -   |   -";
            }

            if (hasHand) {
                std::cout << "  <== [Clock Pointer]";
            }
            std::cout << "\n";
        }
        std::cout << std::string(62, '#') << "\n";
        std::cout << " Metrics: hits: " << statsHits 
                  << " | misses: " << statsMisses 
                  << " | evictions: " << statsEvictions 
                  << " | flushes: " << statsWritebacks 
                  << " | ratio: " << std::fixed << std::setprecision(1) << getHitRate() << "%\n";
        std::cout << std::string(62, '#') << "\n\n";
    }

private:
    // Core Clock Sweep Eviction (Requires mutex lock)
    size_t selectVictimSlot() {
        // First check to confirm at least one block is unpinned
        bool blockAvailable = false;
        for (const auto& b : pool) {
            if (b.isEmpty == false && b.pinRefs == 0) {
                blockAvailable = true;
                break;
            }
        }
        if (!blockAvailable) {
            throw std::runtime_error("Resource Exhaustion: All cache blocks are pinned.");
        }

        // Search circular buffer starting from pointerIndex
        while (true) {
            CacheBlock& block = pool[pointerIndex];
            
            // Skip pinned blocks immediately
            if (block.isEmpty == false && block.pinRefs > 0) {
                pointerIndex = (pointerIndex + 1) % maxFrames;
                continue;
            }
            
            // If no second chance remains, this is our victim
            if (!block.secondChance) {
                size_t targetIndex = pointerIndex;
                pointerIndex = (pointerIndex + 1) % maxFrames;
                return targetIndex;
            }
            
            // Reset second chance and move index forward
            block.secondChance = false;
            pointerIndex = (pointerIndex + 1) % maxFrames;
        }
    }

    // Worker thread loop for page aging decay
    void runJanitorCycle() {
        while (true) {
            std::unique_lock<std::mutex> guard(lock);
            
            // Wake if janitorIntervalMs passes or stopJanitor is called
            if (cv.wait_for(guard, std::chrono::milliseconds(janitorIntervalMs), [this]() { return stopJanitor; })) {
                break; // Stop signal received, leave thread loop
            }
            
            // Decay second chances
            bool modifiedAny = false;
            for (size_t i = 0; i < maxFrames; ++i) {
                if (pool[i].isEmpty == false && pool[i].secondChance && pool[i].pinRefs == 0) {
                    pool[i].secondChance = false;
                    modifiedAny = true;
                }
            }
            
            if (verboseLogging && modifiedAny) {
                std::cout << "[Janitor Services] Performed page aging: decayed active second-chance bits." << std::endl;
            }
        }
    }

    // Instance fields
    size_t maxFrames;
    std::vector<CacheBlock> pool;
    std::unordered_map<Key, size_t, Hash> mapTable; // Key -> index in pool
    size_t pointerIndex;

    // Multithreading and workers
    std::thread janitorThread;
    mutable std::mutex lock;
    std::condition_variable cv;
    bool stopJanitor;
    unsigned int janitorIntervalMs;
    bool verboseLogging;

    // Measurement Metrics
    size_t statsHits;
    size_t statsMisses;
    size_t statsEvictions;
    size_t statsWritebacks;
};

#endif // CLOCK_SWEEP_HPP
