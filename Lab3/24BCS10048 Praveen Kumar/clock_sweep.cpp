/*
 * =============================================================================
 *  Clock Sweep Buffer Pool Replacement
 * =============================================================================
 *
 *  Course  : Advanced DBMS (Scaler)
 *  Author  : Praveen Kumar
 *  Date    : 2026-05-17
 *
 *  Purpose : Implement the clock sweep (second-chance) page replacement
 *            algorithm as used in PostgreSQL's buffer manager.  The program
 *            simulates a fixed-size buffer pool, processes a stream of page
 *            requests, and shows how the clock hand sweeps through the pool
 *            to find eviction candidates.
 *
 *  Build   : g++ -std=c++17 -O2 -o clock_sweep clock_sweep.cpp
 *  Run     : ./clock_sweep
 * =============================================================================
 */

#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <cstdint>

/* ===========================================================================
 *  BufferDescriptor -- one slot in the buffer pool
 * ===========================================================================
 *
 *  In a real DBMS (e.g. PostgreSQL) each buffer descriptor tracks:
 *    - Which disk page is loaded in this slot
 *    - A "usage count" (or reference/pin count)
 *    - Dirty flag, valid flag, etc.
 *
 *  For the clock sweep algorithm the key field is usage_count.  Every time
 *  a page is accessed, its usage_count is incremented (up to a cap).
 *  When the clock hand visits a slot during eviction:
 *    - If usage_count > 0, decrement it and move on (second chance).
 *    - If usage_count == 0, this slot is the eviction victim.
 * -------------------------------------------------------------------------- */

struct BufferDescriptor {
    int      page_id;       /* which page is stored here (-1 = empty)  */
    int      usage_count;   /* "popularity" counter                    */
    bool     dirty;         /* has the page been modified?              */
    bool     valid;         /* is there a page loaded at all?           */

    BufferDescriptor() : page_id(-1), usage_count(0), dirty(false), valid(false) {}
};


/* ===========================================================================
 *  BufferPool -- a fixed-size pool with clock sweep replacement
 * ===========================================================================
 *
 *  PostgreSQL's shared buffer pool works almost exactly like this:
 *    - N buffer slots, each holding one 8 KB page.
 *    - A global "clock hand" index that advances circularly.
 *    - On a page request:
 *        1. Search the pool for the requested page (buffer hit).
 *        2. If not found, sweep the clock to find a victim:
 *           a. If usage_count > 0, decrement and advance.
 *           b. If usage_count == 0, evict this slot.
 *        3. Load the requested page into the victim slot.
 *
 *  This gives frequently-accessed pages more chances to survive eviction,
 *  approximating LRU behavior without maintaining an expensive LRU list.
 * -------------------------------------------------------------------------- */

class BufferPool {
public:
    explicit BufferPool(int pool_size)
        : pool_(pool_size), clock_hand_(0), hits_(0), misses_(0)
    {
    }

    /* -----------------------------------------------------------------------
     *  request_page -- the main entry point
     *
     *  Returns true on a buffer hit, false on a miss (eviction required).
     * ----------------------------------------------------------------------- */
    bool request_page(int page_id, bool trace = true)
    {
        /* Step 1: Check if the page is already in the pool (buffer hit). */
        for (int i = 0; i < (int)pool_.size(); ++i) {
            if (pool_[i].valid && pool_[i].page_id == page_id) {
                pool_[i].usage_count++;     /* boost its survival chances */
                hits_++;
                if (trace) print_hit(page_id, i);
                return true;
            }
        }

        /* Step 2: Page not found -- we need a free or victim slot. */
        misses_++;
        int victim = find_victim(trace);

        /* Step 3: Evict the old page (if any) and load the new one. */
        if (trace && pool_[victim].valid) {
            print_evict(pool_[victim].page_id, victim);
        }

        pool_[victim].page_id     = page_id;
        pool_[victim].usage_count = 1;      /* start with usage = 1 */
        pool_[victim].dirty       = false;
        pool_[victim].valid       = true;

        if (trace) print_load(page_id, victim);
        return false;
    }

    /* -----------------------------------------------------------------------
     *  dump -- print the current state of the buffer pool
     * ----------------------------------------------------------------------- */
    void dump() const
    {
        std::cout << "\n  Buffer Pool State";
        std::cout << "  (clock hand -> slot " << clock_hand_ << ")\n";
        std::cout << "  +------+--------+-------+-------+\n";
        std::cout << "  | Slot |  Page  | Usage | Dirty |\n";
        std::cout << "  +------+--------+-------+-------+\n";

        for (int i = 0; i < (int)pool_.size(); ++i) {
            std::cout << "  |  " << std::setw(2) << i << "  | ";
            if (pool_[i].valid) {
                std::cout << std::setw(5) << pool_[i].page_id << "  |"
                          << std::setw(4) << pool_[i].usage_count << "   |"
                          << (pool_[i].dirty ? "  yes  " : "  no   ");
            } else {
                std::cout << "  --   |   --  |  --    ";
            }
            std::cout << "|";
            if (i == clock_hand_) std::cout << " <-- clock hand";
            std::cout << "\n";
        }
        std::cout << "  +------+--------+-------+-------+\n";
    }

    void print_stats() const
    {
        int total = hits_ + misses_;
        double ratio = total > 0 ? (100.0 * hits_ / total) : 0.0;
        std::cout << "\n  Stats\n";
        std::cout << "    Total requests : " << total << "\n";
        std::cout << "    Hits           : " << hits_ << "\n";
        std::cout << "    Misses         : " << misses_ << "\n";
        std::cout << "    Hit ratio      : " << std::fixed << std::setprecision(1)
                  << ratio << "%\n";
    }

private:
    std::vector<BufferDescriptor> pool_;
    int clock_hand_;
    int hits_;
    int misses_;

    /* -----------------------------------------------------------------------
     *  find_victim -- sweep the clock to find a slot with usage_count == 0
     *
     *  This is the core of the algorithm.  The hand moves circularly through
     *  the buffer pool.  For each slot it visits:
     *    - Empty slot?  Use it immediately (fast path).
     *    - usage_count > 0?  Decrement and move on (give a second chance).
     *    - usage_count == 0?  This is our victim.
     *
     *  In the worst case the hand goes around the entire pool twice:
     *    - First pass decrements everything from 1 to 0.
     *    - Second pass finds the first 0 and stops.
     * ----------------------------------------------------------------------- */
    int find_victim(bool trace)
    {
        int n = (int)pool_.size();
        int steps = 0;

        while (true) {
            BufferDescriptor &slot = pool_[clock_hand_];

            /* Empty slot -- use it right away. */
            if (!slot.valid) {
                int victim = clock_hand_;
                clock_hand_ = (clock_hand_ + 1) % n;
                return victim;
            }

            /* Usage count is zero -- this slot loses. */
            if (slot.usage_count == 0) {
                int victim = clock_hand_;
                clock_hand_ = (clock_hand_ + 1) % n;
                return victim;
            }

            /* Usage count > 0 -- decrement (second chance) and advance. */
            if (trace) {
                std::cout << "    clock @" << clock_hand_
                          << ": page " << slot.page_id
                          << " usage " << slot.usage_count
                          << " -> " << (slot.usage_count - 1)
                          << " (skip)\n";
            }
            slot.usage_count--;
            clock_hand_ = (clock_hand_ + 1) % n;

            steps++;
            /* Safety: should never happen with correct logic, but guard against
               infinite loops during development. */
            if (steps > 2 * n) {
                /* Force-evict current slot. */
                int victim = clock_hand_;
                clock_hand_ = (clock_hand_ + 1) % n;
                return victim;
            }
        }
    }

    /* ---- Trace output helpers ---- */

    void print_hit(int page_id, int slot) const
    {
        std::cout << "  >> Page " << page_id
                  << " : HIT in slot " << slot
                  << " (usage now " << pool_[slot].usage_count << ")\n";
    }

    void print_evict(int page_id, int slot) const
    {
        std::cout << "    evict page " << page_id
                  << " from slot " << slot << "\n";
    }

    void print_load(int page_id, int slot) const
    {
        std::cout << "  >> Page " << page_id
                  << " : MISS -> loaded into slot " << slot << "\n";
    }
};


/* ===========================================================================
 *  main -- run a demo workload
 * ===========================================================================
 *
 *  We simulate a buffer pool with 4 slots and a sequence of page accesses
 *  that shows:
 *    - Cold start (filling empty slots)
 *    - Buffer hits (boosting usage counts)
 *    - Clock sweep eviction (decrementing and finding victims)
 *    - How "hot" pages survive eviction due to higher usage counts
 * -------------------------------------------------------------------------- */

int main()
{
    const int POOL_SIZE = 4;

    std::cout << "============================================================\n";
    std::cout << "  Clock Sweep Buffer Pool Replacement\n";
    std::cout << "============================================================\n";
    std::cout << "\n  Pool size: " << POOL_SIZE << " slots\n\n";

    BufferPool pool(POOL_SIZE);

    /*
     * Workload explanation:
     *   Pages 10, 20, 30, 40 fill up the pool.
     *   Page 10 is accessed again (hit, usage goes up).
     *   Page 50 arrives -- needs eviction.  The clock sweeps through:
     *     - slot 0 (page 10, usage=2): decrement to 1, skip
     *     - slot 1 (page 20, usage=1): decrement to 0, skip
     *     - slot 2 (page 30, usage=1): decrement to 0, skip
     *     - slot 3 (page 40, usage=1): decrement to 0, skip
     *     - slot 0 (page 10, usage=1): decrement to 0, skip
     *     - slot 1 (page 20, usage=0): EVICT
     *   Page 60 arrives, page 30 gets evicted (usage=0 at slot 2).
     *   Page 10 hit again, then page 70 arrives.
     */

    int workload[] = {10, 20, 30, 40, 10, 50, 60, 10, 70, 20, 10, 30};
    int n = sizeof(workload) / sizeof(workload[0]);

    for (int i = 0; i < n; ++i) {
        std::cout << "------------------------------------------------------------\n";
        std::cout << "  Request #" << (i + 1) << ": page " << workload[i] << "\n";
        pool.request_page(workload[i]);
        pool.dump();
    }

    std::cout << "============================================================\n";
    pool.print_stats();
    std::cout << "\n============================================================\n";
    std::cout << "  Done.\n";
    std::cout << "============================================================\n";

    return 0;
}
