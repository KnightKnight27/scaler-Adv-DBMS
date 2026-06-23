#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

template <typename T>
class ClockSweep {
public:
    explicit ClockSweep(std::size_t maxNumber) : maxCacheSize(maxNumber) {
        if (maxCacheSize == 0) {
            throw std::invalid_argument("cache size must be greater than zero");
        }
        frames.reserve(maxCacheSize);
    }

    T getKey(const T& key) {
        const auto frame = pageTable.find(key);
        if (frame == pageTable.end()) {
            return T{};
        }
        frames[frame->second].referenceBit = true;
        return frames[frame->second].key;
    }

    void putKey(const T& key) {
        const auto existingFrame = pageTable.find(key);
        if (existingFrame != pageTable.end()) {
            frames[existingFrame->second].referenceBit = true;
            return;
        }

        if (frames.size() < maxCacheSize) {
            frames.push_back({key, true});
            pageTable[key] = frames.size() - 1;
            return;
        }

        const std::size_t victim = findVictimFrame();
        pageTable.erase(frames[victim].key);
        frames[victim] = {key, true};
        pageTable[key] = victim;
        clockHand = nextFrame(victim);
    }

    bool contains(const T& key) const {
        return pageTable.find(key) != pageTable.end();
    }

    void printState(const std::string& label) const {
        std::cout << label << '\n';
        std::cout << "Clock hand: " << clockHand << '\n';
        for (std::size_t i = 0; i < frames.size(); ++i) {
            std::cout << "Frame " << i << " -> key: " << frames[i].key
                      << ", reference bit: " << frames[i].referenceBit << '\n';
        }
        std::cout << '\n';
    }

private:
    struct Frame {
        T key;
        bool referenceBit;
    };

    std::size_t findVictimFrame() {
        while (true) {
            if (!frames[clockHand].referenceBit) {
                return clockHand;
            }
            frames[clockHand].referenceBit = false;
            clockHand = nextFrame(clockHand);
        }
    }

    std::size_t nextFrame(std::size_t frame) const {
        return (frame + 1) % maxCacheSize;
    }

    std::size_t maxCacheSize{0};
    std::size_t clockHand{0};
    std::vector<Frame> frames;
    std::unordered_map<T, std::size_t> pageTable;
};

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "   CLOCK SWEEP PAGE REPLACEMENT ALGORITHM   " << std::endl;
    std::cout << "============================================" << std::endl;

    ClockSweep<int> clockSweep(3);

    clockSweep.putKey(10);
    clockSweep.putKey(20);
    clockSweep.putKey(30);
    clockSweep.printState("After inserting keys: 10, 20, and 30");

    std::cout << "Accessing keys 10 and 20..." << std::endl;
    clockSweep.getKey(10);
    clockSweep.getKey(20);
    clockSweep.printState("After accessing keys: 10 and 20 (Ref bits updated)");

    std::cout << "Inserting key 40 (Triggers page replacement)..." << std::endl;
    clockSweep.putKey(40);
    clockSweep.printState("After inserting key 40");

    std::cout << "Contains 10: " << (clockSweep.contains(10) ? "YES" : "NO") << '\n';
    std::cout << "Contains 30: " << (clockSweep.contains(30) ? "YES" : "NO") << '\n';
    std::cout << "Contains 40: " << (clockSweep.contains(40) ? "YES" : "NO") << '\n';

    return 0;
}