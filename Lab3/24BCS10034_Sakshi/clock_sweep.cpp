/*
 * clock_sweep.cpp
 *
 * Template implementation — included by clock_sweep.h (NOT compiled separately).
 * This is the standard pattern for C++ template classes.
 */

#pragma once   // guard against double-include when used as header

#include <stdexcept>
#include <iostream>
#include <chrono>

// ── Constructor ───────────────────────────────────────────────────────────────
template <typename T>
ClockSweep<T>::ClockSweep(int maxNumber)
    : maxCacheSize(static_cast<uint>(maxNumber)),
      frames(maxNumber)        // pre-allocate all frames (mirrors PG's fixed buffer pool)
{
    if (maxNumber <= 0)
        throw std::invalid_argument("Buffer pool size must be > 0");

    // Start the background clock thread (mirrors PostgreSQL's bgwriter / checkpointer)
    bgClockThread = std::thread(&ClockSweep<T>::bgWorker, this);

    std::cout << "[ClockSweep] Buffer pool initialised. Capacity = "
              << maxCacheSize << " frames.\n";
}

// ── Destructor ────────────────────────────────────────────────────────────────
template <typename T>
ClockSweep<T>::~ClockSweep() {
    {
        std::lock_guard<std::mutex> lk(mtx);
        stopBg = true;
    }
    bgCV.notify_all();

    if (bgClockThread.joinable())
        bgClockThread.join();

    std::cout << "[ClockSweep] Shut down. Frames evicted cleanly.\n";
}

// ── getKey ────────────────────────────────────────────────────────────────────
//
//  PostgreSQL analogy: ReadBuffer() — pins a buffer and increments its
//  usage count so the clock sweep will not evict it while it's "in use".
//
template <typename T>
T ClockSweep<T>::getKey(T key) {
    std::lock_guard<std::mutex> lk(mtx);

    auto it = keyToFrame.find(key);
    if (it == keyToFrame.end()) {
        // CACHE MISS
        throw std::runtime_error("[ClockSweep] MISS: key not in buffer pool");
    }

    // CACHE HIT — give the frame a second chance
    int idx = it->second;
    frames[idx].usageCount = std::min(frames[idx].usageCount + 1, 5); // cap at 5 like PG

    std::cout << "[ClockSweep] HIT  key=" << key
              << "  frame=" << idx
              << "  usage=" << frames[idx].usageCount << "\n";

    return frames[idx].key;
}

// ── putKey ────────────────────────────────────────────────────────────────────
//
//  PostgreSQL analogy: BufferAlloc() — finds a free or victim buffer,
//  loads the new page, and pins it.
//
template <typename T>
void ClockSweep<T>::putKey(T key) {
    std::lock_guard<std::mutex> lk(mtx);

    // Don't insert a duplicate
    if (keyToFrame.count(key)) {
        std::cout << "[ClockSweep] PUT  key=" << key << "  (already in pool, skipped)\n";
        return;
    }

    int targetFrame = -1;

    if (usedFrames < static_cast<int>(maxCacheSize)) {
        // Free slot available — find the first unoccupied frame
        for (int i = 0; i < static_cast<int>(maxCacheSize); ++i) {
            if (!frames[i].occupied) {
                targetFrame = i;
                break;
            }
        }
        ++usedFrames;
    } else {
        // Pool is full — run clock sweep to find a victim
        targetFrame = findVictim();
    }

    // Load new key into the target frame
    frames[targetFrame].key        = key;
    frames[targetFrame].usageCount = 1;    // newly loaded page gets usage = 1
    frames[targetFrame].occupied   = true;
    keyToFrame[key]                = targetFrame;

    std::cout << "[ClockSweep] PUT  key=" << key
              << "  frame=" << targetFrame
              << "  usage=1\n";
}

// ── findVictim ────────────────────────────────────────────────────────────────
//
//  This is the CORE of Clock Sweep — the part PostgreSQL is famous for.
//
//  The clock hand sweeps the circular buffer:
//    • usage_count > 0  →  decrement it, advance hand (second chance)
//    • usage_count == 0 →  this frame is the VICTIM, evict it
//
//  Worst case: one full revolution decrements every frame to 0,
//  then a second revolution finds the victim. O(N) but very cache-friendly.
//
template <typename T>
int ClockSweep<T>::findVictim() {
    // Safety: this loop WILL terminate because after at most 2 full sweeps
    // every frame will have usage_count == 0.
    while (true) {
        Frame& f = frames[clockHand];

        if (f.usageCount > 0) {
            // Second chance: give this page another life
            --f.usageCount;
            std::cout << "[ClockSweep] SWEEP frame=" << clockHand
                      << "  key=" << f.key
                      << "  usage decremented to " << f.usageCount << "\n";
        } else {
            // usage_count == 0 → evict this frame
            int victim = clockHand;
            evict(victim);

            // Advance hand PAST the victim for next time
            clockHand = (clockHand + 1) % static_cast<int>(maxCacheSize);
            return victim;
        }

        // Advance clock hand (circular)
        clockHand = (clockHand + 1) % static_cast<int>(maxCacheSize);
    }
}

// ── evict ─────────────────────────────────────────────────────────────────────
template <typename T>
void ClockSweep<T>::evict(int frameIdx) {
    Frame& f = frames[frameIdx];
    std::cout << "[ClockSweep] EVICT frame=" << frameIdx
              << "  key=" << f.key << "\n";

    keyToFrame.erase(f.key);
    f.occupied   = false;
    f.usageCount = 0;
    // usedFrames stays the same — putKey immediately places new key here
}

// ── bgWorker ──────────────────────────────────────────────────────────────────
//
//  Background thread: every few seconds, passively decrements usage counts.
//  This mirrors PostgreSQL's bgwriter, which also does background buffer
//  housekeeping so the foreground clock sweep finds victims faster.
//
template <typename T>
void ClockSweep<T>::bgWorker() {
    while (true) {
        // Wait for 3 seconds or until shutdown signal
        std::unique_lock<std::mutex> lk(mtx);
        bgCV.wait_for(lk, std::chrono::seconds(3),
                      [this] { return stopBg.load(); });

        if (stopBg) break;

        std::cout << "[bgWorker] Running background usage-count decay...\n";
        for (auto& f : frames) {
            if (f.occupied && f.usageCount > 0) {
                --f.usageCount;
            }
        }
    }
    std::cout << "[bgWorker] Stopped.\n";
}

// ── Utility ───────────────────────────────────────────────────────────────────
template <typename T>
void ClockSweep<T>::printState() const {
    std::lock_guard<std::mutex> lk(mtx);
    std::cout << "\n┌─────────────────────────────────────────┐\n";
    std::cout << "│  Buffer Pool State  (hand @ frame "
              << clockHand << ")    │\n";
    std::cout << "├──────┬──────────────┬──────────────────────┤\n";
    std::cout << "│Frame │ Key          │ Usage Count          │\n";
    std::cout << "├──────┼──────────────┼──────────────────────┤\n";
    for (int i = 0; i < static_cast<int>(maxCacheSize); ++i) {
        const auto& f = frames[i];
        std::cout << "│  " << i
                  << (i == clockHand ? " ◄" : "  ")
                  << "  │  ";
        if (f.occupied)
            std::cout << f.key << "            │  " << f.usageCount;
        else
            std::cout << "(empty)       │  -";
        std::cout << "                   │\n";
    }
    std::cout << "└──────┴──────────────┴──────────────────────┘\n\n";
}

template <typename T>
bool ClockSweep<T>::isFull() const {
    std::lock_guard<std::mutex> lk(mtx);
    return usedFrames == static_cast<int>(maxCacheSize);
}

template <typename T>
int ClockSweep<T>::size() const {
    std::lock_guard<std::mutex> lk(mtx);
    return usedFrames;
}