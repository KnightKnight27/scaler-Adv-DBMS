#include <iostream>
#include <vector>
#include <unordered_map>
#include <optional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <iomanip>

// ============================================================================
// Clock-Sweep Buffer Replacement Algorithm
// -----------------------------------------
// An approximation of LRU used by PostgreSQL's shared buffer pool.
//
// - Circular buffer of frames, each carrying a usage counter.
// - On access (get/put) the counter is incremented (capped at MAX_USAGE).
// - On eviction the clock hand sweeps: decrement non-zero counters,
//   evict the first frame whose counter reaches 0.
// - A background thread periodically ages all counters so that
//   stale entries are gradually evicted.
// ============================================================================

static constexpr int MAX_USAGE = 5;            // cap for usage counter
static constexpr int BG_SWEEP_INTERVAL_MS = 200; // background sweep period

// ---------------------------------------------------------------------------
// Frame – one slot in the buffer pool
// ---------------------------------------------------------------------------
template<typename T>
struct Frame {
    T        key{};
    int      usageCount{0};
    bool     valid{false};   // true if this slot holds a cached key
    bool     dirty{false};   // true if the page has been modified
};

// ---------------------------------------------------------------------------
// ClockSweep – buffer manager with clock-sweep eviction
// ---------------------------------------------------------------------------
template<typename T>
class ClockSweep {
public:
    // -----------------------------------------------------------------------
    // Constructor – allocates the buffer and starts the background sweeper
    // -----------------------------------------------------------------------
    explicit ClockSweep(int maxNumber)
        : maxCacheSize(maxNumber),
          buffer(maxNumber),
          clockHand(0),
          running(true)
    {
        bgClockThread = std::thread(&ClockSweep::backgroundSweep, this);
    }

    // -----------------------------------------------------------------------
    // Destructor – signals the background thread to stop and waits for it
    // -----------------------------------------------------------------------
    ~ClockSweep() {
        running.store(false);
        if (bgClockThread.joinable()) {
            bgClockThread.join();
        }
    }

    // -----------------------------------------------------------------------
    // getKey – look up a key in the buffer
    //
    // Returns the key wrapped in std::optional on a hit,
    // or std::nullopt on a miss.
    // On a hit the usage counter is incremented (capped at MAX_USAGE).
    // -----------------------------------------------------------------------
    std::optional<T> getKey(T key) {
        std::lock_guard<std::mutex> lock(mtx);

        auto it = keyToFrame.find(key);
        if (it == keyToFrame.end()) {
            // Cache miss
            return std::nullopt;
        }

        // Cache hit – boost usage counter
        int idx = it->second;
        if (buffer[idx].usageCount < MAX_USAGE) {
            buffer[idx].usageCount++;
        }
        return buffer[idx].key;
    }

    // -----------------------------------------------------------------------
    // putKey – insert (or refresh) a key in the buffer
    //
    // 1. If the key is already cached, refresh its usage counter.
    // 2. If a free slot exists, place the key there.
    // 3. Otherwise run clock-sweep eviction to find a victim slot.
    // -----------------------------------------------------------------------
    void putKey(T key) {
        std::lock_guard<std::mutex> lock(mtx);

        // --- Case 1: key already present – refresh it ----------------------
        auto it = keyToFrame.find(key);
        if (it != keyToFrame.end()) {
            int idx = it->second;
            buffer[idx].usageCount = MAX_USAGE;   // boost to max on write
            buffer[idx].dirty = true;
            return;
        }

        // --- Case 2: free slot available -----------------------------------
        for (int i = 0; i < maxCacheSize; ++i) {
            if (!buffer[i].valid) {
                placeKey(i, key);
                return;
            }
        }

        // --- Case 3: buffer full – evict via clock sweep -------------------
        int victim = findVictim();
        evict(victim);
        placeKey(victim, key);
    }

    // -----------------------------------------------------------------------
    // printState – dump the current buffer contents (for demonstration)
    // -----------------------------------------------------------------------
    void printState(const std::string& label) {
        std::lock_guard<std::mutex> lock(mtx);

        std::cout << "\n=== " << label << " ===" << std::endl;
        std::cout << std::left
                  << std::setw(8)  << "Slot"
                  << std::setw(10) << "Key"
                  << std::setw(8)  << "Usage"
                  << std::setw(8)  << "Valid"
                  << std::setw(8)  << "Dirty"
                  << std::endl;
        std::cout << std::string(42, '-') << std::endl;

        for (int i = 0; i < maxCacheSize; ++i) {
            auto& f = buffer[i];
            std::cout << std::left
                      << std::setw(8)  << i;
            if (f.valid) {
                std::cout << std::setw(10) << f.key
                          << std::setw(8)  << f.usageCount
                          << std::setw(8)  << "Y"
                          << std::setw(8)  << (f.dirty ? "Y" : "N");
            } else {
                std::cout << std::setw(10) << "-"
                          << std::setw(8)  << "-"
                          << std::setw(8)  << "N"
                          << std::setw(8)  << "-";
            }
            std::cout << std::endl;
        }

        std::cout << "Clock hand -> slot " << clockHand << std::endl;
    }

private:
    int                          maxCacheSize;
    std::vector<Frame<T>>        buffer;          // the frame pool
    std::unordered_map<T, int>   keyToFrame;      // key -> frame index
    int                          clockHand;       // current sweep position
    std::mutex                   mtx;             // guards all shared state
    std::atomic<bool>            running;         // controls bg thread
    std::thread                  bgClockThread;   // background sweeper

    // -----------------------------------------------------------------------
    // findVictim – sweep the clock hand to locate an eviction candidate
    //
    // Skips invalid slots.  Decrements non-zero usage counters.
    // Returns the index of the first frame whose counter reaches 0.
    // -----------------------------------------------------------------------
    int findVictim() {
        while (true) {
            Frame<T>& frame = buffer[clockHand];

            if (frame.valid) {
                if (frame.usageCount == 0) {
                    int victim = clockHand;
                    advanceHand();
                    return victim;
                }
                frame.usageCount--;
            }

            advanceHand();
        }
    }

    // -----------------------------------------------------------------------
    // evict – remove the entry in the given slot from the buffer
    // -----------------------------------------------------------------------
    void evict(int idx) {
        Frame<T>& f = buffer[idx];
        if (f.dirty) {
            // In a real DBMS this would flush the page to disk.
            std::cout << "[evict] flushing dirty page with key "
                      << f.key << " from slot " << idx << std::endl;
        } else {
            std::cout << "[evict] evicting clean page with key "
                      << f.key << " from slot " << idx << std::endl;
        }
        keyToFrame.erase(f.key);
        f.valid      = false;
        f.usageCount = 0;
        f.dirty      = false;
    }

    // -----------------------------------------------------------------------
    // placeKey – write a key into the given slot
    // -----------------------------------------------------------------------
    void placeKey(int idx, T key) {
        buffer[idx].key        = key;
        buffer[idx].usageCount = 1;
        buffer[idx].valid      = true;
        buffer[idx].dirty      = false;
        keyToFrame[key]        = idx;
    }

    // -----------------------------------------------------------------------
    // advanceHand – move the clock hand one position forward (wraps around)
    // -----------------------------------------------------------------------
    void advanceHand() {
        clockHand = (clockHand + 1) % maxCacheSize;
    }

    // -----------------------------------------------------------------------
    // backgroundSweep – runs in a dedicated thread
    //
    // Periodically decrements the usage counter of every valid frame by 1.
    // This "ages" cached entries so that pages not accessed recently will
    // eventually reach a counter of 0 and become eviction candidates.
    // -----------------------------------------------------------------------
    void backgroundSweep() {
        while (running.load()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(BG_SWEEP_INTERVAL_MS));

            std::lock_guard<std::mutex> lock(mtx);
            for (int i = 0; i < maxCacheSize; ++i) {
                if (buffer[i].valid && buffer[i].usageCount > 0) {
                    buffer[i].usageCount--;
                }
            }
        }
    }
};

// ===========================================================================
// main – demonstration
// ===========================================================================
int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  Clock-Sweep Buffer Replacement"     << std::endl;
    std::cout << "====================================" << std::endl;

    constexpr int POOL_SIZE = 5;
    ClockSweep<int> cs(POOL_SIZE);

    // --- Phase 1: fill the buffer ----------------------------------------
    std::cout << "\n>>> Inserting keys 1..5 (fills buffer)" << std::endl;
    for (int k = 1; k <= 5; ++k) {
        cs.putKey(k);
    }
    cs.printState("After inserting 1-5");

    // --- Phase 2: access some keys to boost their usage ------------------
    std::cout << "\n>>> Accessing keys 2 and 4 (boosting usage)" << std::endl;
    cs.getKey(2);
    cs.getKey(2);   // usage 2 -> 3
    cs.getKey(4);   // usage 4 -> 2

    cs.printState("After boosting 2 and 4");

    // --- Phase 3: insert two more keys – forces evictions ----------------
    std::cout << "\n>>> Inserting keys 6 and 7 (forces eviction)" << std::endl;
    cs.putKey(6);
    cs.putKey(7);

    cs.printState("After inserting 6 and 7");

    // --- Phase 4: demonstrate cache hit vs miss --------------------------
    std::cout << "\n>>> Testing getKey()" << std::endl;
    auto hit  = cs.getKey(2);
    auto miss = cs.getKey(1);

    std::cout << "  getKey(2) -> "
              << (hit  ? std::to_string(*hit)  : "MISS") << std::endl;
    std::cout << "  getKey(1) -> "
              << (miss ? std::to_string(*miss) : "MISS") << std::endl;

    // --- Phase 5: let background sweep age entries -----------------------
    std::cout << "\n>>> Sleeping 500ms to let background sweep age entries..."
              << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    cs.printState("After background aging");

    // --- Phase 6: refresh key 2, then insert 8 --------------------------
    std::cout << "\n>>> Refreshing key 2 via putKey, then inserting key 8"
              << std::endl;
    cs.putKey(2);   // boosts 2's usage back to MAX
    cs.putKey(8);   // should evict the lowest-usage entry

    cs.printState("Final state");

    std::cout << "\n>>> Done." << std::endl;
    return 0;
}
