// Lab Session 3 — Clock Sweep Buffer Pool Page Replacement Algorithm
// Student : Indrajeet Yadav | Roll No: 23BCS10199
//
// Objective: Implement the ClockSweep algorithm used in PostgreSQL's
// shared buffer manager (src/backend/storage/buffer/freelist.c).
// Understand how it approximates LRU without the overhead of a sorted
// linked list, and why that matters at database scale.
//
// Build: g++ -std=c++17 -Wall -Wextra -O2 clocksweep.cpp -o clocksweep
// Run:   ./clocksweep

#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <iomanip>
#include <cassert>

// ─────────────────────────────────────────────────────────────────────────────
// Frame — one slot in the shared buffer pool
// ─────────────────────────────────────────────────────────────────────────────

struct Frame {
    int  page_id     = -1;  // -1 = empty / no page loaded
    int  usage_count =  0;  // 0–5; incremented on access, decremented by clock hand
    bool pinned      = false; // pinned frames are skipped by the clock hand
    bool dirty       = false; // would need writeback before eviction in a real DB

    bool empty() const { return page_id == -1; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Statistics collected during simulation
// ─────────────────────────────────────────────────────────────────────────────

struct Stats {
    int hits         = 0;  // page already in pool when requested
    int misses       = 0;  // page not in pool — must evict + load
    int evictions    = 0;  // frames overwritten
    int dirty_writes = 0;  // evictions that required a "disk write" (dirty flush)
    int sweeps       = 0;  // total clock hand advances during victim search

    double hit_ratio() const {
        int total = hits + misses;
        return total ? 100.0 * hits / total : 0.0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// BufferPool — fixed-size array of frames, managed by Clock Sweep
// ─────────────────────────────────────────────────────────────────────────────

class BufferPool {
public:
    explicit BufferPool(int capacity, int max_usage = 5)
        : capacity_(capacity), max_usage_(max_usage), frames_(capacity) {}

    // ── fetch: bring page_id into the pool (pin it, bump usage_count) ────────
    // Returns the frame index, or -1 if all frames are pinned.
    int fetch(int page_id, bool will_write = false) {
        // ── Case 1: page already in pool (cache hit) ─────────────────────────
        auto it = page_map_.find(page_id);
        if (it != page_map_.end()) {
            int idx = it->second;
            // Increment usage_count, capped at max_usage_ (5 in PostgreSQL)
            frames_[idx].usage_count =
                std::min(frames_[idx].usage_count + 1, max_usage_);
            if (will_write) frames_[idx].dirty = true;
            stats_.hits++;
            log_event("HIT ", page_id, idx, frames_[idx].usage_count);
            return idx;
        }

        // ── Case 2: page not in pool (cache miss) ────────────────────────────
        stats_.misses++;

        // Look for a victim frame via ClockSweep
        int victim = clock_sweep();
        if (victim == -1) {
            std::cerr << "[ERR]  All " << capacity_
                      << " frames are pinned — cannot load page " << page_id << "\n";
            return -1;
        }

        // Evict the current occupant of the victim frame
        if (!frames_[victim].empty()) {
            stats_.evictions++;
            if (frames_[victim].dirty) {
                stats_.dirty_writes++;
                std::cout << "[FLUSH] page " << frames_[victim].page_id
                          << " dirty → writing to disk before eviction\n";
            }
            page_map_.erase(frames_[victim].page_id);
            log_evict(frames_[victim].page_id, victim);
        }

        // Load new page into the victim frame
        frames_[victim] = Frame{page_id, 1, false, will_write};
        page_map_[page_id] = victim;
        log_event("MISS", page_id, victim, 1);
        return victim;
    }

    // ── pin: prevent a frame from being evicted ───────────────────────────────
    // In PostgreSQL, pin_count is the refcount; a page is only evictable when
    // pin_count == 0.
    void pin(int page_id) {
        auto it = page_map_.find(page_id);
        if (it != page_map_.end()) {
            frames_[it->second].pinned = true;
            std::cout << "[PIN]   page " << page_id
                      << " pinned in frame " << it->second << "\n";
        }
    }

    // ── unpin: allow the frame to be evicted again ────────────────────────────
    void unpin(int page_id) {
        auto it = page_map_.find(page_id);
        if (it != page_map_.end()) {
            frames_[it->second].pinned = false;
            std::cout << "[UNPIN] page " << page_id
                      << " unpinned in frame " << it->second << "\n";
        }
    }

    // ── mark_dirty: simulate a write to a buffered page ──────────────────────
    void mark_dirty(int page_id) {
        auto it = page_map_.find(page_id);
        if (it != page_map_.end()) frames_[it->second].dirty = true;
    }

    // ── print_state: ASCII visualization of the buffer pool ──────────────────
    void print_state(const std::string& label = "") const {
        if (!label.empty())
            std::cout << "\n──────────── " << label << " ────────────\n";
        else
            std::cout << "\n──────────── Buffer Pool State ────────────\n";

        std::cout << "  Clock hand at frame " << hand_ << "\n\n";
        std::cout << "  Frame │ Page │ Usage │ Dirty │ Pinned │ Note\n";
        std::cout << "  ──────┼──────┼───────┼───────┼────────┼──────────\n";

        for (int i = 0; i < capacity_; i++) {
            const Frame& f = frames_[i];
            std::cout << "  " << std::setw(5) << i << " │ ";
            if (f.empty())
                std::cout << std::setw(4) << "--" << " │ ";
            else
                std::cout << std::setw(4) << f.page_id << " │ ";

            // usage_count bar
            std::string bar(f.usage_count, '#');
            bar += std::string(max_usage_ - f.usage_count, '.');
            std::cout << " " << bar << " │ ";

            std::cout << (f.dirty  ? "  YES  " : "   no  ") << " │ ";
            std::cout << (f.pinned ? "  YES   " : "   no   ") << " │ ";

            if (i == hand_) std::cout << "◄── hand";
            std::cout << "\n";
        }
        std::cout << "\n  Hit ratio so far: "
                  << std::fixed << std::setprecision(1) << stats_.hit_ratio()
                  << "% (" << stats_.hits << " hits, " << stats_.misses << " misses)\n";
        std::cout << "  Evictions: " << stats_.evictions
                  << "  |  Clock hand advances: " << stats_.sweeps << "\n";
        std::cout << "───────────────────────────────────────────\n\n";
    }

    const Stats& stats() const { return stats_; }

private:
    int                              capacity_;
    int                              max_usage_;
    int                              hand_ = 0;  // clock hand position
    std::vector<Frame>               frames_;
    std::unordered_map<int, int>     page_map_;  // page_id → frame index
    Stats                            stats_;

    // ── clock_sweep: find the next eviction victim ────────────────────────────
    //
    // Algorithm (mirrors PostgreSQL freelist.c):
    //   1. Start at 'hand_'.
    //   2. If frame is pinned → skip.
    //   3. If usage_count > 0 → decrement, skip ("second chance").
    //   4. If usage_count == 0 → this is the victim; advance hand and return.
    //   5. Limit to 2 × capacity sweeps before declaring failure (all pinned).
    //
    // Why two full sweeps?  After one sweep, every frame with usage_count > 0
    // has been decremented at least once. After two sweeps, even a frame with
    // usage_count = max (5) will have reached 0 if it wasn't accessed again.
    int clock_sweep() {
        int checked = 0;
        while (checked < 2 * capacity_) {
            Frame& f = frames_[hand_];

            if (!f.pinned) {
                if (f.usage_count == 0) {
                    // Found a victim at position 'hand_'
                    int victim = hand_;
                    hand_ = (hand_ + 1) % capacity_;  // advance past victim
                    return victim;
                }
                // Give this frame a second chance: decrement usage count
                f.usage_count--;
            }

            hand_ = (hand_ + 1) % capacity_;
            stats_.sweeps++;
            checked++;
        }
        return -1;  // all frames are pinned
    }

    void log_event(const char* type, int page, int frame, int usage) const {
        std::cout << "[" << type << "] page " << std::setw(3) << page
                  << " → frame " << frame
                  << "  usage=" << usage << "\n";
    }

    void log_evict(int old_page, int frame) const {
        std::cout << "[EVICT] page " << std::setw(3) << old_page
                  << " ← frame " << frame << " evicted\n";
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper: run an access sequence and print progress
// ─────────────────────────────────────────────────────────────────────────────

void run_sequence(BufferPool& pool,
                  const std::vector<int>& pages,
                  const std::string& label) {
    std::cout << "\n╔═══════════════════════════════════════════════╗\n";
    std::cout << "  " << label << "\n";
    std::cout << "  Access sequence: ";
    for (int p : pages) std::cout << p << " ";
    std::cout << "\n╚═══════════════════════════════════════════════╝\n\n";

    for (int p : pages) pool.fetch(p);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main: four demonstration scenarios
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Lab 3: Clock Sweep Buffer Pool ===\n"
              << "    Indrajeet Yadav | 23BCS10199\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 1: Basic eviction — 4-frame pool, 5-page working set
    // Demonstrates that pages 1 and 2 survive because they are re-accessed
    // before the clock hand reaches them with usage_count > 0.
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "╔══════════════════════════════════════════════════════╗\n"
                  << "  SCENARIO 1: Basic Eviction (4 frames, pages 1-5)\n"
                  << "╚══════════════════════════════════════════════════════╝\n\n";

        BufferPool pool(4);

        // Fill all 4 frames
        pool.fetch(1); pool.fetch(2); pool.fetch(3); pool.fetch(4);
        pool.print_state("After loading pages 1-4 (pool full)");

        // Re-access pages 1 and 2 — their usage_count goes to 2
        pool.fetch(1); pool.fetch(2);
        pool.print_state("After re-accessing pages 1 and 2");

        // Now access page 5 (miss). Clock sweep must find a victim.
        // Trace:
        //   hand=0, frame[0]=page 1, usage=2 → decrement to 1, skip
        //   hand=1, frame[1]=page 2, usage=2 → decrement to 1, skip
        //   hand=2, frame[2]=page 3, usage=1 → decrement to 0, skip
        //   hand=3, frame[3]=page 4, usage=1 → decrement to 0, skip
        //   hand=0, frame[0]=page 1, usage=1 → decrement to 0, skip
        //   hand=1, frame[1]=page 2, usage=1 → decrement to 0, skip
        //   hand=2, frame[2]=page 3, usage=0 → EVICT! load page 5 here
        std::cout << "Fetching page 5 (miss → must evict):\n";
        pool.fetch(5);
        pool.print_state("After loading page 5 — page 3 evicted (first to reach usage=0)");

        // Fetch page 6 — now page 4 should be the victim
        std::cout << "Fetching page 6 (miss → must evict):\n";
        pool.fetch(6);
        pool.print_state("After loading page 6");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 2: Pinned frames cannot be evicted
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "╔══════════════════════════════════════════════════════╗\n"
                  << "  SCENARIO 2: Pinned Frames Skip Eviction\n"
                  << "╚══════════════════════════════════════════════════════╝\n\n";

        BufferPool pool(3);
        pool.fetch(10); pool.fetch(20); pool.fetch(30);
        pool.print_state("Full pool: pages 10, 20, 30");

        // Pin pages 10 and 20 — simulates an active query holding those pages
        pool.pin(10); pool.pin(20);
        pool.print_state("Pages 10 and 20 pinned");

        // Now fetch page 40 — only page 30 is evictable
        std::cout << "Fetching page 40 — only page 30 can be evicted:\n";
        pool.fetch(40);
        pool.print_state("Page 30 evicted (only non-pinned frame)");

        // Unpin pages 10 and 20
        pool.unpin(10); pool.unpin(20);
        pool.print_state("Pages 10 and 20 unpinned (now evictable)");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 3: Sequential scan flood
    // In pure LRU, a sequential scan (N pages > buffer pool size) would evict
    // ALL hot pages. ClockSweep limits damage because hot pages have usage_count
    // up to 5, so they survive multiple clock sweeps.
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "╔══════════════════════════════════════════════════════╗\n"
                  << "  SCENARIO 3: Sequential Scan Flood (LRU killer)\n"
                  << "╚══════════════════════════════════════════════════════╝\n\n";

        BufferPool pool(6);

        // Load and heavily re-access 'hot' pages (simulate indexed lookups)
        std::cout << "--- Loading hot pages 1-3, accessing each 4 times ---\n";
        for (int p : {1, 2, 3}) {
            pool.fetch(p);
            pool.fetch(p); // +1
            pool.fetch(p); // +1
            pool.fetch(p); // +1 → usage=4
        }
        pool.print_state("Hot pages 1-3 with usage=4");

        // Now a full table scan arrives: pages 101-108 (8 pages > 3 free frames)
        // ClockSweep will evict the cold pages first; hot pages survive longer.
        std::cout << "--- Sequential scan: pages 101..108 ---\n";
        for (int p : {101, 102, 103, 104, 105, 106, 107, 108}) {
            pool.fetch(p);
        }
        pool.print_state("After sequential scan — hot pages may survive");

        // Check if any hot pages survived
        for (int p : {1, 2, 3}) {
            pool.fetch(p);  // hit if still in pool
        }
        pool.print_state("After re-checking hot pages");

        const Stats& s = pool.stats();
        std::cout << "FINAL STATS:\n"
                  << "  Hits:             " << s.hits       << "\n"
                  << "  Misses:           " << s.misses     << "\n"
                  << "  Evictions:        " << s.evictions  << "\n"
                  << "  Clock advances:   " << s.sweeps     << "\n"
                  << "  Hit ratio:        " << std::fixed << std::setprecision(1)
                  << s.hit_ratio() << "%\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 4: Dirty page tracking
    // In a real database, evicting a dirty page requires writing it to disk
    // (or at least to the WAL / checkpoint buffer). This scenario shows that
    // dirty pages incur extra I/O cost at eviction time.
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "╔══════════════════════════════════════════════════════╗\n"
                  << "  SCENARIO 4: Dirty Page Write-back on Eviction\n"
                  << "╚══════════════════════════════════════════════════════╝\n\n";

        BufferPool pool(3);
        pool.fetch(50); pool.fetch(60); pool.fetch(70);

        // Simulate writes to pages 50 and 60
        pool.mark_dirty(50);
        pool.mark_dirty(60);
        pool.print_state("Pages 50 and 60 are dirty (modified but not flushed)");

        // Evict — dirty pages must be written to disk first
        std::cout << "Fetching pages 80 and 90 (must evict dirty pages):\n";
        pool.fetch(80);
        pool.fetch(90);
        pool.print_state("After evictions — dirty pages triggered disk writes");

        std::cout << "Dirty writes (simulated disk flushes): "
                  << pool.stats().dirty_writes << "\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Algorithm walkthrough: step-by-step clock hand trace
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "╔══════════════════════════════════════════════════════╗\n"
              << "  CLOCK SWEEP: Step-by-step hand trace explanation\n"
              << "╚══════════════════════════════════════════════════════╝\n\n";

    std::cout <<
        "  Pool size: 4 frames.  Access sequence: 1 2 3 4 1 2 5\n\n"
        "  After loading 1,2,3,4:\n"
        "    Frame[0] page=1 usage=1   ◄─ hand\n"
        "    Frame[1] page=2 usage=1\n"
        "    Frame[2] page=3 usage=1\n"
        "    Frame[3] page=4 usage=1\n\n"
        "  After HIT on 1,2:\n"
        "    Frame[0] page=1 usage=2   ← elevated\n"
        "    Frame[1] page=2 usage=2   ← elevated\n"
        "    Frame[2] page=3 usage=1\n"
        "    Frame[3] page=4 usage=1\n\n"
        "  Fetch page 5 (MISS) — clock sweep starts at hand=0:\n"
        "    Frame[0] page=1 usage=2 → decrement to 1, advance\n"
        "    Frame[1] page=2 usage=2 → decrement to 1, advance\n"
        "    Frame[2] page=3 usage=1 → decrement to 0, advance\n"
        "    Frame[3] page=4 usage=1 → decrement to 0, advance\n"
        "    Frame[0] page=1 usage=1 → decrement to 0, advance\n"
        "    Frame[1] page=2 usage=1 → decrement to 0, advance\n"
        "    Frame[2] page=3 usage=0 → EVICT! load page 5 here\n\n"
        "  Result: page 3 evicted (lowest usage, first to reach 0).\n"
        "  Pages 1 and 2 survived because their elevated usage_count\n"
        "  gave them 'second chances' to avoid eviction.\n\n"
        "  This is exactly what PostgreSQL does in freelist.c:\n"
        "    StrategyGetBuffer() → ClockSweepTick() → decrement usage_count\n"
        "    → if usage_count reaches 0 and not pinned → victim found\n\n";

    return 0;
}
