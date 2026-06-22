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
    // Structure representing a single cache frame
    struct Frame {
        Key key;
        Value value;
        bool referenceBit = false;
        bool occupied = false;
        bool isDirty = false;
        int pinCount = 0;
    };

    // Constructor: initializes cache structures, clock hand, and starts sweeper thread
    ClockSweep(size_t maxSize, unsigned int sweepIntervalMs = 2000, bool debug = false)
        : maxCacheSize(maxSize),
          clockHand(0),
          stopSweeper(false),
          sweepIntervalMs(sweepIntervalMs),
          debugMode(debug),
          hitCount(0),
          missCount(0),
          evictionCount(0),
          dirtyWritebackCount(0) {
        
        if (maxCacheSize == 0) {
            throw std::invalid_argument("Cache size must be greater than 0");
        }
        
        // Initialize frames vector with empty frames
        frames.resize(maxCacheSize);
        
        // Start background sweeping thread for maintenance/aging
        sweeperThread = std::thread(&ClockSweep::sweepLoop, this);
        
        if (debugMode) {
            std::cout << "[Cache Init] Initialized Clock Sweep Cache with size: " 
                      << maxCacheSize << ", Sweep Interval: " << sweepIntervalMs << "ms" << std::endl;
        }
    }

    // Destructor: signals background thread to stop, joins it, and cleans up
    ~ClockSweep() {
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            stopSweeper = true;
        }
        cvStop.notify_all();
        
        if (sweeperThread.joinable()) {
            sweeperThread.join();
        }
        
        if (debugMode) {
            std::cout << "[Cache Shutdown] Sweeper thread stopped and cache cleared." << std::endl;
        }
    }

    // -------------------------------------------------------------
    // Core Functional API
    // -------------------------------------------------------------

    // Retrieve a value from the cache
    std::optional<Value> get(const Key& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            size_t idx = it->second;
            frames[idx].referenceBit = true; // Mark as recently referenced
            hitCount++;
            
            if (debugMode) {
                std::cout << "[Cache Hit] Key: " << key << " at index: " << idx << std::endl;
            }
            return frames[idx].value;
        }
        
        missCount++;
        if (debugMode) {
            std::cout << "[Cache Miss] Key: " << key << " not in cache." << std::endl;
        }
        return std::nullopt;
    }

    // Retrieve a value with standard boolean output parameter
    bool get(const Key& key, Value& valueOut) {
        std::optional<Value> val = get(key);
        if (val.has_value()) {
            valueOut = val.value();
            return true;
        }
        return false;
    }

    // Insert or update an entry in the cache
    void put(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            // Key already exists: update value and mark referenced
            size_t idx = it->second;
            
            // Check if value actually changed to determine dirty bit
            if (!(frames[idx].value == value)) {
                frames[idx].value = value;
                frames[idx].isDirty = true; // Mark dirty since value modified
            }
            frames[idx].referenceBit = true;
            
            if (debugMode) {
                std::cout << "[Cache Update] Key: " << key << " updated at index: " << idx << std::endl;
            }
            return;
        }
        
        // If cache is full, evict an entry using Clock Sweep algorithm
        if (cacheMap.size() >= maxCacheSize) {
            try {
                size_t victimIndex = findVictimIndex();
                
                // Evict the victim frame
                Frame& victim = frames[victimIndex];
                Key oldKey = victim.key;
                
                if (victim.isDirty) {
                    dirtyWritebackCount++;
                    if (debugMode) {
                        std::cout << "[Disk I/O] Flushing dirty key: " << oldKey 
                                  << " (value: " << victim.value << ") to persistent storage." << std::endl;
                    }
                }
                
                cacheMap.erase(oldKey);
                evictionCount++;
                
                if (debugMode) {
                    std::cout << "[Cache Eviction] Evicted key: " << oldKey 
                              << " from index: " << victimIndex << " to fit key: " << key << std::endl;
                }
                
                // Insert the new entry at the victim's slot
                victim.key = key;
                victim.value = value;
                victim.referenceBit = false; // Initialize to 0 per Section 11 workflow
                victim.occupied = true;
                victim.isDirty = false;
                victim.pinCount = 0;
                
                cacheMap[key] = victimIndex;
                
            } catch (const std::runtime_error& e) {
                // If all frames are pinned, eviction fails
                std::cerr << "[Cache Error] " << e.what() << " Cannot insert key: " << key << std::endl;
                throw; // Rethrow to let caller handle buffer pool depletion
            }
        } else {
            // Cache is not full: find the first unoccupied frame
            for (size_t i = 0; i < maxCacheSize; ++i) {
                if (!frames[i].occupied) {
                    frames[i].key = key;
                    frames[i].value = value;
                    frames[i].referenceBit = false; // Initialize to 0 per Section 11 workflow
                    frames[i].occupied = true;
                    frames[i].isDirty = false;
                    frames[i].pinCount = 0;
                    
                    cacheMap[key] = i;
                    
                    if (debugMode) {
                        std::cout << "[Cache Insert] Key: " << key << " placed in empty frame: " << i << std::endl;
                    }
                    break;
                }
            }
        }
    }

    // -------------------------------------------------------------
    // Database Buffer Manager Pinning & Dirty API (Bonus)
    // -------------------------------------------------------------

    // Pin a frame (prevents it from being evicted)
    bool pin(const Key& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            size_t idx = it->second;
            frames[idx].pinCount++;
            if (debugMode) {
                std::cout << "[Cache Pin] Key: " << key << " pinned (Pin Count: " 
                          << frames[idx].pinCount << ")" << std::endl;
            }
            return true;
        }
        return false;
    }

    // Unpin a frame (allows it to be evicted when count reaches 0)
    bool unpin(const Key& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            size_t idx = it->second;
            if (frames[idx].pinCount > 0) {
                frames[idx].pinCount--;
                if (debugMode) {
                    std::cout << "[Cache Unpin] Key: " << key << " unpinned (Pin Count: " 
                              << frames[idx].pinCount << ")" << std::endl;
                }
                return true;
            }
        }
        return false;
    }

    // Explicitly mark a frame as dirty (modified)
    bool markDirty(const Key& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            size_t idx = it->second;
            frames[idx].isDirty = true;
            if (debugMode) {
                std::cout << "[Cache Dirty] Key: " << key << " marked dirty." << std::endl;
            }
            return true;
        }
        return false;
    }

    // -------------------------------------------------------------
    // Statistics & Debug Utility API
    // -------------------------------------------------------------

    void setDebugMode(bool debug) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        debugMode = debug;
    }

    size_t getHits() const { return hitCount; }
    size_t getMisses() const { return missCount; }
    size_t getEvictions() const { return evictionCount; }
    size_t getDirtyWritebacks() const { return dirtyWritebackCount; }
    
    double getHitRate() const {
        size_t total = hitCount + missCount;
        return total == 0 ? 0.0 : (static_cast<double>(hitCount) / total) * 100.0;
    }

    void resetStats() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        hitCount = 0;
        missCount = 0;
        evictionCount = 0;
        dirtyWritebackCount = 0;
    }

    // Visualizes the circular buffer / clock sweep layout in a beautiful way
    void printCacheState() const {
        std::lock_guard<std::mutex> lock(cacheMutex);
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << " CLOCK SWEEP CACHE VISUALIZER (Capacity: " << cacheMap.size() << "/" << maxCacheSize << ")\n";
        std::cout << std::string(60, '-') << "\n";
        std::cout << "  Index | Occupied | Key | Value | Ref Bit | Dirty | Pin Count\n";
        std::cout << std::string(60, '-') << "\n";

        for (size_t i = 0; i < maxCacheSize; ++i) {
            const Frame& f = frames[i];
            bool isHand = (i == clockHand);
            
            std::cout << "  " << std::setw(5) << i << " | ";
            if (f.occupied) {
                std::cout << "Yes      | " 
                          << std::setw(3) << f.key << " | " 
                          << std::setw(5) << f.value << " | "
                          << "   " << f.referenceBit << "    | "
                          << "  " << f.isDirty << "   | "
                          << "    " << f.pinCount;
            } else {
                std::cout << "No       |  -  |   -   |    -    |   -   |     -";
            }

            if (isHand) {
                std::cout << "  <-- [Clock Hand]";
            }
            std::cout << "\n";
        }
        std::cout << std::string(60, '=') << "\n";
        std::cout << " Stats: Hits: " << hitCount 
                  << " | Misses: " << missCount 
                  << " | Evictions: " << evictionCount 
                  << " | Dirty Write-backs: " << dirtyWritebackCount
                  << " | Hit Rate: " << std::fixed << std::setprecision(1) << getHitRate() << "%\n";
        std::cout << std::string(60, '=') << "\n\n";
    }

private:
    // Core Clock Sweep Eviction Logic (requires cacheMutex to be locked)
    size_t findVictimIndex() {
        // Ensure at least one frame is unpinned to prevent infinite loops
        bool allPinned = true;
        for (const auto& f : frames) {
            if (f.occupied && f.pinCount == 0) {
                allPinned = false;
                break;
            }
        }
        if (allPinned) {
            throw std::runtime_error("All occupied frames are pinned! Cache is fully locked.");
        }

        // Loop through the circular array starting at the clock hand
        while (true) {
            Frame& frame = frames[clockHand];
            
            // If the frame at the clock hand is pinned, skip it immediately (do not touch its reference bit)
            if (frame.occupied && frame.pinCount > 0) {
                clockHand = (clockHand + 1) % maxCacheSize;
                continue;
            }
            
            // If reference bit is 0, this frame is our victim for eviction
            if (!frame.referenceBit) {
                size_t victimIndex = clockHand;
                // Move clock hand to the next frame
                clockHand = (clockHand + 1) % maxCacheSize;
                return victimIndex;
            }
            
            // Reference bit is 1: give second chance
            frame.referenceBit = false;
            // Advance the clock hand
            clockHand = (clockHand + 1) % maxCacheSize;
        }
    }

    // Background thread loop for periodic reference bit decay (aging)
    void sweepLoop() {
        while (true) {
            std::unique_lock<std::mutex> lock(cacheMutex);
            
            // Wait for sweepIntervalMs or stop signal
            if (cvStop.wait_for(lock, std::chrono::milliseconds(sweepIntervalMs), [this]() { return stopSweeper; })) {
                break; // Stop signal received, exit background loop
            }
            
            // Age the pages: clear the reference bits of pages that haven't been accessed
            // This is similar to a clock sweep page cleaner thread in a database buffer pool
            bool agedAny = false;
            for (size_t i = 0; i < maxCacheSize; ++i) {
                if (frames[i].occupied && frames[i].referenceBit && frames[i].pinCount == 0) {
                    frames[i].referenceBit = false;
                    agedAny = true;
                }
            }
            
            if (debugMode && agedAny) {
                std::cout << "[Background Sweeper] Performed periodic page reference aging (cleared active reference bits)." << std::endl;
            }
        }
    }

    // Private Member Variables
    size_t maxCacheSize;
    std::vector<Frame> frames;
    std::unordered_map<Key, size_t, Hash> cacheMap; // Maps Key to vector index for O(1) lookup
    size_t clockHand;

    // Background sweep synchronization
    std::thread sweeperThread;
    mutable std::mutex cacheMutex;
    std::condition_variable cvStop;
    bool stopSweeper;
    unsigned int sweepIntervalMs;
    bool debugMode;

    // Cache Statistics
    size_t hitCount;
    size_t missCount;
    size_t evictionCount;
    size_t dirtyWritebackCount;
};

#endif // CLOCK_SWEEP_HPP
