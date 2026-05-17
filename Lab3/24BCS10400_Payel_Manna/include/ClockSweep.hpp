#pragma once

#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <iostream>

template<typename T>
class ClockSweep {

public:

    explicit ClockSweep(size_t maxNumber)
        : maxCacheSize(maxNumber),
          cache(maxNumber)
    {
        bgClockThread =
            std::thread(
                &ClockSweep::backgroundSweep,
                this
            );
    }

    ~ClockSweep() {

        running.store(false);

        if (bgClockThread.joinable()) {
            bgClockThread.join();
        }
    }

    bool getKey(const T& key) {

        std::shared_lock lock(tableMutex);

        auto it = pageTable.find(key);

        if (it == pageTable.end()) {
            return false;
        }

        int idx = it->second;

        Frame& frame = cache[idx];

        {
            std::lock_guard frameLock(frame.frameMutex);

            frame.referenceBit = true;
        }

        return true;
    }

    void putKey(const T& key) {

        {
            std::shared_lock readLock(tableMutex);

            auto it = pageTable.find(key);

            if (it != pageTable.end()) {

                Frame& frame = cache[it->second];

                std::lock_guard frameLock(frame.frameMutex);

                frame.referenceBit = true;

                return;
            }
        }

        int victim = findVictim();

        {
            std::unique_lock writeLock(tableMutex);

            Frame& frame = cache[victim];

            frame.key = key;
            frame.occupied = true;
            frame.referenceBit = true;

            pageTable[key] = victim;
        }
    }

    void printCache() {

        std::shared_lock lock(tableMutex);

        std::cout << "\nCache State:\n";

        for (size_t i = 0; i < maxCacheSize; ++i) {

            Frame& frame = cache[i];

            std::lock_guard frameLock(frame.frameMutex);

            if (frame.occupied) {

                std::cout
                    << "["
                    << frame.key
                    << " | ref="
                    << frame.referenceBit
                    << "] ";
            }
            else {

                std::cout << "[empty] ";
            }
        }

        std::cout << "\n";
    }

private:

    struct Frame {

        T key;

        bool occupied = false;

        bool referenceBit = false;

        std::mutex frameMutex;
    };

    size_t maxCacheSize{0};

    std::vector<Frame> cache;

    std::unordered_map<T, int> pageTable;

    std::shared_mutex tableMutex;

    std::thread bgClockThread;

    std::atomic<bool> running{true};

    size_t clockHand{0};

private:

    int findVictim() {

        while (true) {

            Frame& frame = cache[clockHand];

            {
                std::lock_guard frameLock(frame.frameMutex);

                if (!frame.occupied) {

                    int victim = clockHand;

                    moveClockHand();

                    return victim;
                }

                if (frame.referenceBit) {

                    frame.referenceBit = false;
                }
                else {

                    int victim = clockHand;

                    {
                        std::unique_lock writeLock(tableMutex);

                        pageTable.erase(frame.key);
                    }

                    frame.occupied = false;

                    moveClockHand();

                    return victim;
                }
            }

            moveClockHand();
        }
    }

    void moveClockHand() {

        clockHand =
            (clockHand + 1) % maxCacheSize;
    }

    void backgroundSweep() {

        while (running.load()) {

            std::this_thread::sleep_for(
                std::chrono::seconds(1)
            );

            for (auto& frame : cache) {

                std::lock_guard frameLock(
                    frame.frameMutex
                );

                if (frame.referenceBit) {

                    frame.referenceBit = false;
                }
            }
        }
    }
};