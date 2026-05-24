#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>

// Clock-sweep buffer cache (PostgreSQL-style usage counter variant).
//
// Each frame keeps a small saturating use-count (0..kMaxUseCnt). Get/Put
// on an already-cached key bumps the count up to the cap. When a new key
// arrives and no free slot is left, the clock hand sweeps frames:
//   - if use_cnt > 0, decrement and advance,
//   - if use_cnt == 0, evict that frame and load the new key there.
// Capacity is rounded up to a power of two so hand advancement is a
// bitmask AND instead of a modulo.
template <typename T>
class ClockSweep {
public:
    explicit ClockSweep(size_t capacity)
        : frames(RoundUpToPowerOfTwo(capacity)),
          handMask(frames.empty() ? 0 : frames.size() - 1) {}

    // True on hit (and bumps the use count). False on miss.
    bool Get(const T& key) {
        auto it = pageTable.find(key);
        if (it == pageTable.end()) return false;
        Touch(frames[it->second]);
        return true;
    }

    // Insert key. If already present, just bumps use count. If the
    // cache is full, runs the clock sweep to find a victim.
    void Put(const T& key) {
        if (frames.empty()) return;

        auto it = pageTable.find(key);
        if (it != pageTable.end()) {
            Touch(frames[it->second]);
            return;
        }

        if (nextFree < frames.size()) {
            LoadIntoFrame(nextFree++, key);
            return;
        }

        // Sweep is bounded: after at most (kMaxUseCnt + 1) full passes,
        // some frame's use_cnt must have hit zero.
        const size_t maxScans = static_cast<size_t>(kMaxUseCnt + 1) * frames.size();
        for (size_t scanned = 0; scanned < maxScans; ++scanned) {
            Frame& f = frames[hand];
            if (f.useCnt > 0) {
                --f.useCnt;
                AdvanceHand();
                continue;
            }
            EvictFrame(hand);
            LoadIntoFrame(hand, key);
            AdvanceHand();
            return;
        }
    }

    void DebugPrint() const {
        for (const Frame& f : frames) {
            if (f.valid) {
                std::cout << '[' << f.key
                          << " u=" << static_cast<unsigned>(f.useCnt) << "] ";
            } else {
                std::cout << "[free] ";
            }
        }
        std::cout << " hand=" << hand << "\n";
    }

private:
    struct Frame {
        T key{};
        uint8_t useCnt{0};
        bool valid{false};
    };

    static constexpr uint8_t kMaxUseCnt = 5;

    static size_t RoundUpToPowerOfTwo(size_t v) {
        if (v == 0) return 0;
        --v;
        for (size_t s = 1; s < sizeof(size_t) * 8; s <<= 1) v |= (v >> s);
        return v + 1;
    }

    void Touch(Frame& f) {
        if (f.useCnt < kMaxUseCnt) ++f.useCnt;
    }

    void AdvanceHand() { hand = (hand + 1) & handMask; }

    void EvictFrame(size_t idx) {
        Frame& f = frames[idx];
        if (!f.valid) return;
        pageTable.erase(f.key);
        f.valid = false;
        f.useCnt = 0;
    }

    void LoadIntoFrame(size_t idx, const T& key) {
        Frame& f = frames[idx];
        f.key = key;
        f.useCnt = 1;
        f.valid = true;
        pageTable[key] = idx;
    }

    std::vector<Frame> frames;
    std::unordered_map<T, size_t> pageTable;
    size_t hand{0};
    size_t nextFree{0};
    size_t handMask{0};
};

int main() {
    ClockSweep<int> clockSweep(3);

    clockSweep.Put(1);
    clockSweep.Put(2);
    clockSweep.Put(3);
    clockSweep.Get(1);
    clockSweep.Get(1);
    clockSweep.Get(2);
    std::cout << "Before eviction: ";
    clockSweep.DebugPrint();

    clockSweep.Put(4);
    std::cout << "After Put(4):   ";
    clockSweep.DebugPrint();

    return 0;
}
