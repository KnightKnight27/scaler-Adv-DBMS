#include <iostream>
#include <array>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <cstdint>

static constexpr std::size_t MAX_CAPACITY = 64;

// ---------------------------------------------------------------------------
// ClockSweep<T, N>
//
// N  = max number of frames (compile-time constant, default 64)
// T  = value type -- just needs operator== and a copy constructor
//
// Layout is struct-of-arrays: keys, ref bits, and occupied flags are kept
// in separate arrays.  This is friendlier to the hardware prefetcher when
// the sweep only needs to touch ref bits, not full value objects.
//
// No hash map.  Linear scan over N slots.  For a buffer pool N is small
// (hundreds, not millions) and avoiding hashing keeps the code simple and
// predictable.  If N ever grows we can revisit.
// ---------------------------------------------------------------------------
template<typename T, std::size_t N = MAX_CAPACITY>
class ClockSweep {
    static_assert(N > 0, "cache size must be at least 1");

public:
    explicit ClockSweep(std::size_t maxSize, unsigned int sweepIntervalSecs = 5)
        : capacity_(maxSize < N ? maxSize : N)
        , size_(0)
        , hand_(0)
        , stop_(false)
    {
        
        occupied_.fill(false);
        refbit_.fill(0);

        bg_ = std::thread(&ClockSweep::bgLoop, this, sweepIntervalSecs);
    }

    ~ClockSweep() {
        stop_.store(true, std::memory_order_release);
        bg_.join();
    }

    ClockSweep(const ClockSweep&)            = delete;
    ClockSweep& operator=(const ClockSweep&) = delete;

   
    T get(const T& key) {
        std::lock_guard<std::mutex> lk(mtx_);

        int slot = find(key);
        if (slot < 0)
            throw std::runtime_error("cache miss");

        refbit_[slot] = 1;
        return keys_[slot];
    }

    void put(const T& key) {
        std::lock_guard<std::mutex> lk(mtx_);

        // already present?
        int existing = find(key);
        if (existing >= 0) {
            refbit_[existing] = 1;
            return;
        }

        std::size_t slot = (size_ < capacity_) ? claimFreeSlot() : evict();

        keys_[slot]     = key;
        occupied_[slot] = true;
        refbit_[slot]   = 1;
        ++size_;
    }

    // ------------------------------------------------------------------------
    void printState() const {
        std::lock_guard<std::mutex> lk(mtx_);
        std::cout << "[cache] capacity=" << capacity_
                  << " used=" << size_
                  << " hand=" << hand_ << "\n";
        for (std::size_t i = 0; i < capacity_; ++i) {
            std::cout << "  " << i << ": ";
            if (occupied_[i])
                std::cout << keys_[i] << " (ref=" << (int)refbit_[i] << ")";
            else
                std::cout << "--";
            if (i == hand_) std::cout << " <--";
            std::cout << "\n";
        }
    }

private:
    
    int find(const T& key) const {
        for (std::size_t i = 0; i < capacity_; ++i)
            if (occupied_[i] && keys_[i] == key)
                return static_cast<int>(i);
        return -1;
    }

    std::size_t claimFreeSlot() {
        for (std::size_t i = 0; i < capacity_; ++i)
            if (!occupied_[i]) return i;
        // unreachable if caller checks size_ < capacity_ first
        throw std::logic_error("claimFreeSlot called on full cache");
    }

    // ------------------------------------------------------------------------
    // evict -- the actual clock sweep.
    // spins the hand; on ref=1 clears it (second chance); on ref=0 evicts.
    // returns the now-free slot index.  called with lock held.
    // ------------------------------------------------------------------------
    std::size_t evict() {
        // worst case we loop twice around (once clearing refs, once evicting)
        const std::size_t limit = 2 * capacity_;
        for (std::size_t steps = 0; steps < limit; ++steps) {
            std::size_t i = hand_;
            hand_ = (hand_ + 1) % capacity_;

            if (!occupied_[i]) return i;   

            if (refbit_[i]) {
                refbit_[i] = 0;           
            } else {
                occupied_[i] = false;       // evict
                --size_;
                return i;
            }
        }
        std::size_t fallback = hand_;
        occupied_[fallback] = false;
        --size_;
        hand_ = (hand_ + 1) % capacity_;
        return fallback;
    }

    // ------------------------------------------------------------------------
    // bgLoop -- background thread: periodically ages entries by clearing
    // ref bits.  does not evict -- eviction is always demand-driven.
    // ------------------------------------------------------------------------
    void bgLoop(unsigned int intervalSecs) {
        while (!stop_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::seconds(intervalSecs));
            if (stop_.load(std::memory_order_acquire)) break;

            std::lock_guard<std::mutex> lk(mtx_);
            std::cout << "[bg sweep] aging " << size_ << " entries\n";
            for (std::size_t i = 0; i < capacity_; ++i)
                if (occupied_[i]) refbit_[i] = 0;  
        }
    }

    // -- data (struct-of-arrays) ---------------------------------------------
    std::size_t              capacity_;
    std::size_t              size_;
    std::size_t              hand_;

    std::array<T,    N>      keys_;
    std::array<bool, N>      occupied_;
    std::array<uint8_t, N>   refbit_;   

    mutable std::mutex       mtx_;
    std::thread              bg_;
    std::atomic<bool>        stop_;
};

// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== ClockSweep<int> cap=4 ===\n";
    {
        ClockSweep<int, 4> c(4, 2);

        c.put(1); c.put(2); c.put(3); c.put(4);
        c.printState();

        c.get(1); c.get(2);               
        std::cout << "\nafter getting 1 and 2:\n";
        c.printState();

        std::cout << "\nput(5) -- should evict 3 (first cold entry):\n";
        c.put(5);
        c.printState();

        try       { c.get(3); }
        catch (const std::runtime_error& e) { std::cout << "miss: " << e.what() << "\n"; }
    }

    std::cout << "\n=== ClockSweep<std::string> cap=3 ===\n";
    {
        ClockSweep<std::string, 3> c(3, 60);

        c.put("foo"); c.put("bar"); c.put("baz");
        c.get("foo");                        
        c.put("qux");                        
        c.printState();
    }

    return 0;
}