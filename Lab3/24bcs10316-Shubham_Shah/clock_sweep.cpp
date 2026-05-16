#include <cstddef>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

template <typename T>
class ClockSweep {
public:
    explicit ClockSweep(std::size_t maxNumber)
        : maxCacheSize(maxNumber), frames(maxNumber) {
        if (maxCacheSize == 0) {
            throw std::invalid_argument("ClockSweep cache size must be greater than zero");
        }
    }

    ClockSweep(const ClockSweep&) = delete;
    ClockSweep& operator=(const ClockSweep&) = delete;

    ~ClockSweep() {
        if (bgClockThread.joinable()) {
            bgClockThread.join();
        }
    }

    std::optional<T> getKey(const T& key) {
        std::lock_guard<std::mutex> lock(latch);

        auto page = pageTable.find(key);
        if (page == pageTable.end()) {
            return std::nullopt;
        }

        Frame& frame = frames[page->second];
        frame.referenced = true;
        return frame.key;
    }

    void putKey(const T& key) {
        std::lock_guard<std::mutex> lock(latch);

        auto page = pageTable.find(key);
        if (page != pageTable.end()) {
            frames[page->second].referenced = true;
            return;
        }

        while (true) {
            Frame& frame = frames[clockHand];

            if (!frame.key.has_value() || !frame.referenced) {
                writeFrame(clockHand, key);
                advanceClockHand();
                return;
            }

            frame.referenced = false;
            advanceClockHand();
        }
    }

    bool contains(const T& key) const {
        std::lock_guard<std::mutex> lock(latch);
        return pageTable.find(key) != pageTable.end();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(latch);
        return pageTable.size();
    }

    std::size_t capacity() const {
        return maxCacheSize;
    }

    std::size_t handPosition() const {
        std::lock_guard<std::mutex> lock(latch);
        return clockHand;
    }

    std::vector<std::pair<T, bool>> snapshot() const {
        std::lock_guard<std::mutex> lock(latch);

        std::vector<std::pair<T, bool>> currentFrames;
        currentFrames.reserve(frames.size());

        for (const Frame& frame : frames) {
            if (frame.key.has_value()) {
                currentFrames.emplace_back(*frame.key, frame.referenced);
            }
        }

        return currentFrames;
    }

private:
    struct Frame {
        std::optional<T> key;
        bool referenced{false};
    };

    void advanceClockHand() {
        clockHand = (clockHand + 1) % maxCacheSize;
    }

    void writeFrame(std::size_t index, const T& key) {
        if (frames[index].key.has_value()) {
            pageTable.erase(*frames[index].key);
        }

        frames[index].key = key;
        frames[index].referenced = true;
        pageTable[key] = index;
    }

    const std::size_t maxCacheSize{0};
    std::vector<Frame> frames;
    std::unordered_map<T, std::size_t> pageTable;
    std::size_t clockHand{0};
    mutable std::mutex latch;
    std::thread bgClockThread;
};

template <typename T>
void printCacheState(const std::string& label, const ClockSweep<T>& cache) {
    std::cout << label << "\n";
    std::cout << "  hand=" << cache.handPosition() << " size=" << cache.size() << "/"
              << cache.capacity() << " frames=";

    for (const auto& [key, referenced] : cache.snapshot()) {
        std::cout << "[" << key << ":" << (referenced ? "R" : "-") << "] ";
    }

    std::cout << "\n";
}

int main() {
    ClockSweep<int> clockSweep(3);

    clockSweep.putKey(1);
    clockSweep.putKey(2);
    clockSweep.putKey(3);
    printCacheState("After inserting 1, 2, 3", clockSweep);

    clockSweep.getKey(1);
    printCacheState("After reading 1", clockSweep);

    clockSweep.putKey(4);
    printCacheState("After inserting 4", clockSweep);

    clockSweep.getKey(2);
    printCacheState("After reading 2", clockSweep);

    clockSweep.putKey(5);
    printCacheState("After inserting 5", clockSweep);

    std::cout << "Contains 1? " << (clockSweep.contains(1) ? "yes" : "no") << "\n";
    std::cout << "Contains 5? " << (clockSweep.contains(5) ? "yes" : "no") << "\n";

    return 0;
}
