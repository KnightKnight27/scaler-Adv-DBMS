#include <iostream>
#include <vector>
#include <unordered_map>

template<typename T>
class ClockSweep {
private:
    struct Frame {
        T key;
        bool referenceBit;

        Frame(T k) : key(k), referenceBit(true) {}
    };

    unsigned int maxCacheSize;
    std::vector<Frame> cache;
    std::unordered_map<T, int> indexMap;
    int clockHand;

public:
    ClockSweep(int maxNumber)
        : maxCacheSize(maxNumber), clockHand(0) {}

    T getKey(T key) {
        if (indexMap.find(key) != indexMap.end()) {
            int idx = indexMap[key];
            cache[idx].referenceBit = true;

            std::cout << "Key Found: " << key << std::endl;
            return key;
        }

        std::cout << "Key Not Found\n";
        return T();
    }

    void putKey(T key) {

        if (indexMap.find(key) != indexMap.end()) {
            int idx = indexMap[key];
            cache[idx].referenceBit = true;

            std::cout << "Key Updated: " << key << std::endl;
            return;
        }

        if (cache.size() < maxCacheSize) {
            cache.push_back(Frame(key));
            indexMap[key] = cache.size() - 1;

            std::cout << "Inserted: " << key << std::endl;
            return;
        }

        while (true) {

            if (!cache[clockHand].referenceBit) {

                T oldKey = cache[clockHand].key;

                indexMap.erase(oldKey);

                cache[clockHand] = Frame(key);

                indexMap[key] = clockHand;

                std::cout << "Replaced " << oldKey
                          << " with " << key << std::endl;

                clockHand = (clockHand + 1) % maxCacheSize;

                break;
            }

            cache[clockHand].referenceBit = false;

            clockHand = (clockHand + 1) % maxCacheSize;
        }
    }

    void displayCache() {
        std::cout << "Cache State:\n";

        for (auto &frame : cache) {
            std::cout << frame.key
                      << " (" << frame.referenceBit << ") "
                      << std::endl;
        }

        std::cout << "------------------\n";
    }
};

int main() {

    ClockSweep<int> clockSweep(3);

    clockSweep.putKey(1);
    clockSweep.putKey(2);
    clockSweep.putKey(3);

    clockSweep.displayCache();

    clockSweep.getKey(1);

    clockSweep.putKey(4);

    clockSweep.displayCache();

    return 0;
}