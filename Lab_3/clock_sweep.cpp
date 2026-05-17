#include <iostream>
#include <unordered_map>
#include <vector>
#include <optional>
#include <string>

template<typename Key, typename Value>
class ClockSweepCache {
private:

    struct PageFrame {
        Key key;
        Value value;
        bool referenceBit = false;
        bool occupied = false;
    };

    std::vector<PageFrame> buffer;
    std::unordered_map<Key, int> pageMap;

    int capacity;
    int hand;

public:

    explicit ClockSweepCache(int size)
        : capacity(size), hand(0) {

        if (size <= 0) {
            throw std::invalid_argument("Cache size must be greater than 0");
        }

        buffer.resize(size);
    }

    void insert(const Key& key, const Value& value) {

        // Update existing page
        if (pageMap.find(key) != pageMap.end()) {

            int idx = pageMap[key];

            buffer[idx].value = value;
            buffer[idx].referenceBit = true;

            std::cout << "Updated existing page: "
                      << key << std::endl;

            return;
        }

        // Search empty frame
        for (int i = 0; i < capacity; i++) {

            if (!buffer[i].occupied) {

                buffer[i].key = key;
                buffer[i].value = value;
                buffer[i].occupied = true;
                buffer[i].referenceBit = true;

                pageMap[key] = i;

                std::cout << "Inserted page "
                          << key
                          << " into frame "
                          << i
                          << std::endl;

                return;
            }
        }

        // Clock sweep replacement
        replacePage(key, value);
    }

    std::optional<Value> access(const Key& key) {

        if (pageMap.find(key) == pageMap.end()) {

            std::cout << "Page Miss: "
                      << key
                      << std::endl;

            return std::nullopt;
        }

        int idx = pageMap[key];

        buffer[idx].referenceBit = true;

        std::cout << "Page Hit: "
                  << key
                  << " found in frame "
                  << idx
                  << std::endl;

        return buffer[idx].value;
    }

    void displayBuffer() const {

        std::cout << "\n===== BUFFER STATE =====\n";

        std::cout << "Frame\tKey\tValue\tRefBit\n";

        for (int i = 0; i < capacity; i++) {

            std::cout << i << "\t";

            if (buffer[i].occupied) {

                std::cout
                    << buffer[i].key << "\t"
                    << buffer[i].value << "\t"
                    << (buffer[i].referenceBit ? "1" : "0");

            } else {

                std::cout << "-\t-\t-";
            }

            std::cout << std::endl;
        }

        std::cout << "Clock Hand Position: "
                  << hand
                  << std::endl;
    }

private:

    void replacePage(const Key& key, const Value& value) {

        std::cout << "\nBuffer Full -> Starting Clock Sweep\n";

        while (true) {

            if (!buffer[hand].referenceBit) {

                std::cout
                    << "Evicting page "
                    << buffer[hand].key
                    << " from frame "
                    << hand
                    << std::endl;

                pageMap.erase(buffer[hand].key);

                buffer[hand].key = key;
                buffer[hand].value = value;
                buffer[hand].referenceBit = true;
                buffer[hand].occupied = true;

                pageMap[key] = hand;

                std::cout
                    << "Inserted page "
                    << key
                    << " into frame "
                    << hand
                    << std::endl;

                moveHand();

                break;
            }

            std::cout
                << "Frame "
                << hand
                << " given second chance\n";

            buffer[hand].referenceBit = false;

            moveHand();
        }
    }

    void moveHand() {
        hand = (hand + 1) % capacity;
    }
};

int main() {

    std::cout << "===== Clock Sweep Algorithm Demo =====\n";

    ClockSweepCache<int, std::string> cache(3);

    cache.insert(1, "PageA");
    cache.insert(2, "PageB");
    cache.insert(3, "PageC");

    cache.displayBuffer();

    cache.access(1);
    cache.access(2);

    cache.insert(4, "PageD");

    cache.displayBuffer();

    cache.access(3);

    cache.insert(5, "PageE");

    cache.displayBuffer();

    return 0;
}