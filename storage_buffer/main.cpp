#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

template<typename T>
class ClockSweep {

public:

    /*
        Each frame represents one page in cache.

        occupied     -> whether frame contains valid data
        referenceBit -> used by Clock Sweep algorithm
        dirty        -> page modified or not
    */
    struct Frame {

        T key{};

        bool occupied{false};

        bool referenceBit{false};

        bool dirty{false};
    };

public:

    /*
        Constructor

        Initializes:
        - cache size
        - frame array
        - background clock thread
    */
    ClockSweep(int maxNumber)
        : maxCacheSize(maxNumber),
          frames(maxNumber),
          stopThread(false),
          clockHand(0)
    {
        bgClockThread =
            std::thread(&ClockSweep::backgroundSweep, this);
    }

    /*
        Destructor

        Stops background thread safely.
    */
    ~ClockSweep() {

        stopThread = true;

        if(bgClockThread.joinable()) {
            bgClockThread.join();
        }
    }

    /*
        GET OPERATION

        If key exists:
        - set reference bit
        - return true

        Otherwise:
        - cache miss
        - return false
    */
    bool getKey(T key) {

        std::lock_guard<std::mutex> lock(cacheMutex);

        auto it = pageTable.find(key);

        // Cache miss
        if(it == pageTable.end()) {

            std::cout << "MISS: " << key << "\n";

            return false;
        }

        // Cache hit
        int index = it->second;

        // Give second chance
        frames[index].referenceBit = true;

        std::cout << "HIT: " << key << "\n";

        return true;
    }

    /*
        PUT OPERATION

        Case 1:
        If key already exists:
            update reference bit

        Case 2:
        If empty frame available:
            insert there

        Case 3:
        Cache full:
            evict victim using Clock Sweep
    */
    void putKey(T key) {

        std::lock_guard<std::mutex> lock(cacheMutex);

        // Check whether key already exists
        auto found = pageTable.find(key);

        if(found != pageTable.end()) {

            int idx = found->second;

            frames[idx].referenceBit = true;

            std::cout << "UPDATED: "
                      << key
                      << "\n";

            return;
        }

        // Search for empty frame
        for(int i = 0; i < maxCacheSize; i++) {

            if(!frames[i].occupied) {

                frames[i].key = key;

                frames[i].occupied = true;

                frames[i].referenceBit = true;

                pageTable[key] = i;

                std::cout << "INSERTED: "
                          << key
                          << " into frame "
                          << i
                          << "\n";

                return;
            }
        }

        /*
            Cache is full

            Need eviction using Clock Sweep
        */
        int victim = findVictim();

        std::cout << "EVICTING: "
                  << frames[victim].key
                  << "\n";

        // Remove old mapping
        pageTable.erase(frames[victim].key);

        // Insert new page
        frames[victim].key = key;

        frames[victim].referenceBit = true;

        frames[victim].occupied = true;

        // Update hash table
        pageTable[key] = victim;

        std::cout << "INSERTED: "
                  << key
                  << " into frame "
                  << victim
                  << "\n";
    }

    /*
        Utility function to display cache state
    */
    void printCache() {

        std::lock_guard<std::mutex> lock(cacheMutex);

        std::cout << "\n========== CACHE STATE ==========\n";

        for(int i = 0; i < maxCacheSize; i++) {

            if(frames[i].occupied) {

                std::cout << "Frame "
                          << i
                          << " -> Key: "
                          << frames[i].key
                          << " | RefBit: "
                          << frames[i].referenceBit
                          << "\n";
            }
            else {

                std::cout << "Frame "
                          << i
                          << " -> EMPTY\n";
            }
        }

        std::cout << "=================================\n\n";
    }

private:

    /*
        Clock Sweep Victim Selection

        Algorithm:

        If reference bit == 0:
            select victim

        Else:
            clear reference bit
            move clock hand
    */
    int findVictim() {

        while(true) {

            // If page not recently used
            if(!frames[clockHand].referenceBit) {

                int victim = clockHand;

                advanceClock();

                return victim;
            }

            // Give second chance
            frames[clockHand].referenceBit = false;

            advanceClock();
        }
    }

    /*
        Circular movement of clock hand
    */
    void advanceClock() {

        clockHand =
            (clockHand + 1) % maxCacheSize;
    }

    /*
        Background Clock Thread

        Runs every second.

        Simulates PostgreSQL-like aging:
        pages gradually lose reference bit.
    */
    void backgroundSweep() {

        while(!stopThread) {

            {
                std::lock_guard<std::mutex> lock(cacheMutex);

                std::cout
                    << "\n[Background Clock Sweep Running]\n";

                for(int i = 0; i < maxCacheSize; i++) {

                    if(frames[i].occupied &&
                       frames[i].referenceBit) {

                        // Aging step
                        frames[i].referenceBit = false;
                    }
                }
            }

            // Run every 1 second
            std::this_thread::sleep_for(
                std::chrono::seconds(1)
            );
        }
    }

private:

    // Maximum cache capacity
    uint maxCacheSize{0u};

    // Actual frame storage
    std::vector<Frame> frames;

    // Maps key -> frame index
    std::unordered_map<T, int> pageTable;

    // Background thread
    std::thread bgClockThread;

    // Thread safety lock
    std::mutex cacheMutex;

    // Thread termination flag
    std::atomic<bool> stopThread;

    // Current clock hand position
    int clockHand;
};

int main() {

    // Create cache with capacity = 4
    ClockSweep<int> clockSweep(4);

    /*
        Initial insertions
    */
    clockSweep.putKey(1);
    clockSweep.putKey(2);
    clockSweep.putKey(3);
    clockSweep.putKey(4);

    clockSweep.printCache();

    /*
        Access some pages

        Their reference bits become 1
    */
    clockSweep.getKey(1);
    clockSweep.getKey(2);

    /*
        Wait for background sweep

        It clears reference bits gradually
    */
    std::this_thread::sleep_for(
        std::chrono::seconds(2)
    );

    /*
        Cache full now.

        Clock Sweep eviction happens.
    */
    clockSweep.putKey(5);

    clockSweep.printCache();

    clockSweep.putKey(6);

    clockSweep.printCache();

    return 0;
}