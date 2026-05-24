#pragma once

#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <iostream>
#include <stdexcept>

/*
 * ClockSweep<T> — PostgreSQL-style buffer replacement policy
 *
 * How PostgreSQL Clock Sweep works:
 * -------------------------------------------------
 * Each buffer frame has a "usage count" (0–5 in real PG).
 * A clock hand sweeps around a circular array of frames.
 *
 *   • On ACCESS  → increment usage count (pin the frame)
 *   • On EVICT   → sweep the clock hand:
 *       - If usage_count > 0  → decrement it, advance hand (second chance)
 *       - If usage_count == 0 → EVICT this frame, place new key here
 *
 * This gives frequently-used pages a "second chance" before eviction,
 * approximating LRU without the overhead of maintaining an LRU list.
 */

template <typename T>
class ClockSweep {
public:
    // ── Constructor ──────────────────────────────────────────────────────────
    explicit ClockSweep(int maxNumber);

    // ── Destructor — stops background thread cleanly ─────────────────────────
    ~ClockSweep();

    /*
     * getKey(key)
     * -----------
     * Looks up `key` in the buffer pool.
     *   • HIT  → increments usage count, returns key.
     *   • MISS → throws std::runtime_error (caller decides how to load page).
     */
    T getKey(T key);

    /*
     * putKey(key)
     * -----------
     * Inserts `key` into the buffer pool.
     *   • If pool has free slots → place directly.
     *   • If pool is full       → run clock sweep to find a victim frame,
     *                             evict it, insert new key at that position.
     */
    void putKey(T key);

    // ── Utility ──────────────────────────────────────────────────────────────
    void printState() const;   // debug: show all frames + usage counts
    bool isFull()     const;
    int  size()       const;

private:
    // ── Frame ─────────────────────────────────────────────────────────────────
    struct Frame {
        T    key;
        int  usageCount{0};   // 0 = evictable; >0 = has been used recently
        bool occupied{false};
    };

    // ── Internal helpers ──────────────────────────────────────────────────────
    int  findVictim();         // run the clock hand, return victim index
    void evict(int frameIdx);  // evict frame at index

    // ── Data members ──────────────────────────────────────────────────────────
    const uint              maxCacheSize;
    std::vector<Frame>      frames;          // circular buffer of frames
    int                     clockHand{0};    // current position of clock hand
    int                     usedFrames{0};   // how many frames are occupied

    std::unordered_map<T, int> keyToFrame;   // key → frame index (fast lookup)

    mutable std::mutex      mtx;             // protects all state

    // ── Background eviction thread (mirrors PG's bgwriter) ───────────────────
    std::thread             bgClockThread;
    std::atomic<bool>       stopBg{false};
    std::condition_variable bgCV;

    void bgWorker();   // background thread: periodically scans & decrements usage counts
};

// ── Include template implementation ──────────────────────────────────────────
#include "clock_sweep.cpp"