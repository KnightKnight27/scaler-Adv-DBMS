// Lab 3 - Clock Sweep page replacement (PostgreSQL style buffer pool)
// Akshat Kushwaha | 24BCS10060
//
// A buffer pool of fixed size that decides which page to throw out using the
// "clock sweep" algorithm, the same idea PostgreSQL uses in its shared buffer
// manager. Instead of a single reference bit, each frame keeps a small
// usage_count (0..MAX_USAGE). A circular "hand" walks the frames:
//    - if usage_count > 0  -> lower it by one and keep walking (second chance)
//    - if usage_count == 0 -> evict this frame
// Every access bumps usage_count back up (capped), so hot pages survive.
//
// Build: g++ -std=c++17 -Wall -Wextra clock_sweep.cpp -o clock_sweep
// Run:   ./clock_sweep

#include <iostream>
#include <unordered_map>
#include <vector>

namespace {
constexpr int kMaxUsage = 3;   // PostgreSQL caps this at 5; 3 is enough to show
}

class BufferPool {
public:
    explicit BufferPool(int size) : frames_(size), hand_(0) {}

    // Request a page. Returns the frame index it ends up in. Prints what
    // happened (hit / miss / which page got evicted) so the trace is readable.
    int request(int page) {
        auto found = location_.find(page);
        if (found != location_.end()) {
            Frame& f = frames_[found->second];
            f.usage = std::min(f.usage + 1, kMaxUsage);
            std::cout << "  HIT    page " << page
                      << " (frame " << found->second
                      << ", usage now " << f.usage << ")\n";
            return found->second;
        }

        int slot = choose_victim();
        Frame& f = frames_[slot];
        if (f.page != kEmpty) {
            std::cout << "  EVICT  page " << f.page
                      << " from frame " << slot << "\n";
            location_.erase(f.page);
        }
        f.page  = page;
        f.usage = 1;                 // new page starts warm so it isn't instantly evicted
        location_[page] = slot;
        std::cout << "  MISS   page " << page
                  << " loaded into frame " << slot << "\n";
        return slot;
    }

    void dump() const {
        std::cout << "  state: ";
        for (int i = 0; i < static_cast<int>(frames_.size()); ++i) {
            const Frame& f = frames_[i];
            std::cout << "[";
            if (f.page == kEmpty) std::cout << "--";
            else                  std::cout << "p" << f.page << ":u" << f.usage;
            std::cout << (i == hand_ ? "<hand]" : "]");
            std::cout << " ";
        }
        std::cout << "\n";
    }

private:
    static constexpr int kEmpty = -1;
    struct Frame {
        int page  = kEmpty;
        int usage = 0;
    };

    // Walk the clock hand until a frame with usage 0 is found, lowering the
    // usage of every frame we pass. An empty frame counts as a free victim.
    int choose_victim() {
        const int n = static_cast<int>(frames_.size());
        while (true) {
            Frame& f = frames_[hand_];
            if (f.page == kEmpty || f.usage == 0) {
                int victim = hand_;
                hand_ = (hand_ + 1) % n;
                return victim;
            }
            --f.usage;                       // give it a second chance
            hand_ = (hand_ + 1) % n;
        }
    }

    std::vector<Frame>          frames_;
    std::unordered_map<int,int> location_;   // page -> frame index
    int                         hand_;
};

int main() {
    std::cout << "Clock Sweep buffer pool demo (4 frames, max usage "
              << kMaxUsage << ")\n";
    std::cout << "Akshat Kushwaha | 24BCS10060\n\n";

    BufferPool pool(4);

    // Access pattern: pages 7 and 8 are touched often (hot), the rest are
    // touched once. The hot pages should still be resident at the end.
    const std::vector<int> pattern = {7, 8, 9, 10, 7, 8, 11, 7, 8, 12, 9, 7};

    for (int page : pattern) {
        std::cout << "request page " << page << ":\n";
        pool.request(page);
        pool.dump();
        std::cout << "\n";
    }

    std::cout << "Notice pages 7 and 8 stayed in the pool the longest because\n"
                 "their usage counts kept getting refreshed.\n";
    return 0;
}
