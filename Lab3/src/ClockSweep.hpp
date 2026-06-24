#pragma once

#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>

template <typename T>
class ClockSweep {
public:
    explicit ClockSweep(size_t capacity)
        : capacity(capacity), frames(capacity), clockHand(0), running(true)
    {
        ager = std::thread(&ClockSweep::backgroundAge, this);
    }

    ~ClockSweep() {
        running.store(false);
        if (ager.joinable()) ager.join();
    }

    // returns true if key is in cache
    bool getKey(const T& key) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = pageTable.find(key);
        if (it == pageTable.end()) return false;
        frames[it->second].refBit = true;
        return true;
    }

    void putKey(const T& key) {
        std::lock_guard<std::mutex> lock(mtx);

        // already in cache
        if (pageTable.count(key)) {
            frames[pageTable[key]].refBit = true;
            return;
        }

        int victim = findVictim();
        if (frames[victim].occupied)
            pageTable.erase(frames[victim].key);

        frames[victim] = {key, true, true};
        pageTable[key] = victim;
    }

    void printCache() {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "\nCache State:\n";
        for (size_t i = 0; i < capacity; i++) {
            if (frames[i].occupied)
                std::cout << "  [" << frames[i].key << " | ref=" << frames[i].refBit << "]\n";
            else
                std::cout << "  [empty]\n";
        }
    }

private:
    struct Frame {
        T key;
        bool occupied = false;
        bool refBit   = false;
    };

    size_t capacity;
    std::vector<Frame> frames;
    std::unordered_map<T, int> pageTable;
    size_t clockHand;
    std::mutex mtx;
    std::thread ager;
    std::atomic<bool> running;

    int findVictim() {
        while (true) {
            Frame& f = frames[clockHand];
            if (!f.occupied || !f.refBit) {
                int v = clockHand;
                clockHand = (clockHand + 1) % capacity;
                return v;
            }
            f.refBit = false;
            clockHand = (clockHand + 1) % capacity;
        }
    }

    // periodically clears ref bits so inactive pages age out
    void backgroundAge() {
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::lock_guard<std::mutex> lock(mtx);
            for (auto& f : frames)
                if (f.occupied) f.refBit = false;
        }
    }
};
