#include <iostream>
#include <unordered_map>
#include <vector>
#include <optional>
#include <string>
#include <stdexcept>

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
        // Update existing page (Page Hit / Re-write)
        if (pageMap.find(key) != pageMap.end()) {
            int idx = pageMap[key];
            buffer[idx].value = value;
            buffer[idx].referenceBit = true;
            std::cout << "[INSERT/UPDATE] Key: " << key 
                      << " already exists. Updated value, set RefBit = 1.\n";
            return;
        }

        // Search for an unoccupied frame (initial population)
        for (int i = 0; i < capacity; i++) {
            if (!buffer[i].occupied) {
                buffer[i].key = key;
                buffer[i].value = value;
                buffer[i].occupied = true;
                buffer[i].referenceBit = true;
                pageMap[key] = i;
                std::cout << "[INSERT] Key: " << key 
                          << " placed in unoccupied Frame: " << i 
                          << " (RefBit = 1)\n";
                return;
            }
        }

        // Buffer is full -> Trigger Clock Sweep Eviction
        replacePage(key, value);
    }

    std::optional<Value> access(const Key& key) {
        if (pageMap.find(key) == pageMap.end()) {
            std::cout << "[ACCESS] Key: " << key << " -> PAGE MISS\n";
            return std::nullopt;
        }

        int idx = pageMap[key];
        buffer[idx].referenceBit = true;
        std::cout << "[ACCESS] Key: " << key << " -> PAGE HIT (Frame: " 
                  << idx << ", RefBit updated to 1)\n";
        return buffer[idx].value;
    }

    void displayBuffer() const {
        std::cout << "\n------------------ BUFFER STATE ------------------\n";
        std::cout << "Frame\tKey\tValue\t\tRefBit\tOccupied\n";
        for (int i = 0; i < capacity; i++) {
            std::cout << i << "\t";
            if (buffer[i].occupied) {
                std::cout << buffer[i].key << "\t"
                          << buffer[i].value << "\t\t"
                          << (buffer[i].referenceBit ? "1" : "0") << "\t"
                          << "Yes\n";
            } else {
                std::cout << "-\t-\t-\t\t-\tNo\n";
            }
        }
        std::cout << "Clock Hand Position: Frame " << hand << "\n";
        std::cout << "--------------------------------------------------\n\n";
    }

private:
    void replacePage(const Key& key, const Value& value) {
        std::cout << "\n[EVICTION TRIGGERED] Cache is Full. Initiating Clock Sweep...\n";
        while (true) {
            std::cout << " Examining Frame " << hand 
                      << " (Key: " << buffer[hand].key 
                      << ", RefBit: " << (buffer[hand].referenceBit ? "1" : "0") << ")\n";

            if (!buffer[hand].referenceBit) {
                // Evict page with referenceBit = 0
                std::cout << " >>> Evicting page " << buffer[hand].key 
                          << " from Frame " << hand << " (Second chance expired/unused)\n";
                
                pageMap.erase(buffer[hand].key);

                buffer[hand].key = key;
                buffer[hand].value = value;
                buffer[hand].referenceBit = true;
                buffer[hand].occupied = true;
                pageMap[key] = hand;

                std::cout << " >>> Inserted new page " << key 
                          << " into Frame " << hand << " (RefBit = 1)\n";
                
                moveHand();
                break;
            }

            // Reference bit is 1 -> give second chance and reset bit
            std::cout << "   RefBit is 1. Giving second chance, resetting RefBit to 0.\n";
            buffer[hand].referenceBit = false;
            moveHand();
        }
    }

    void moveHand() {
        hand = (hand + 1) % capacity;
    }
};

int main() {
    std::cout << "========================================================\n";
    std::cout << "  Lab 3: Clock Sweep Cache Replacement Algorithm Demo  \n";
    std::cout << "========================================================\n\n";

    // Task 1: Cache Initialization
    std::cout << "--- TASK 1: Cache Initialization ---\n";
    ClockSweepCache<int, std::string> cache(3);
    cache.displayBuffer();

    // Task 2: Cache Population
    std::cout << "--- TASK 2: Cache Population ---\n";
    cache.insert(1, "PageA");
    cache.insert(2, "PageB");
    cache.insert(3, "PageC");
    cache.displayBuffer();

    // Task 3: Access Pattern Analysis
    std::cout << "--- TASK 3: Access Pattern Analysis (Accessing Pages 1 and 2) ---\n";
    cache.access(1); // Page Hit, RefBit remains/becomes 1
    cache.access(2); // Page Hit, RefBit remains/becomes 1
    // Page 3 was NOT accessed, its RefBit is 1 from insertion, but let's show eviction.
    // To clearly show second-chance sweep: we will trigger eviction.
    // Note: Initially all three pages have RefBit = 1 because they were just inserted.
    cache.displayBuffer();

    // Task 4 & 5: Clock Sweep & Replacement Analysis
    std::cout << "--- TASK 4 & 5: Clock Sweep & Cache Replacement ---\n";
    std::cout << "Inserting Page 4 (expects sweep to clear RefBits of 1 & 2, then evict 3 if hand starts at 0, or evicts based on hand)\n";
    // Since hand is currently at 0 (Frame 0: Key 1, RefBit 1):
    // - Frame 0 has RefBit 1 -> resets to 0, hand moves to 1
    // - Frame 1 has RefBit 1 -> resets to 0, hand moves to 2
    // - Frame 2 (Key 3) has RefBit 1 -> resets to 0, hand moves to 0
    // - Frame 0 has RefBit 0 -> evicted! PageD is inserted into Frame 0.
    cache.insert(4, "PageD");
    cache.displayBuffer();

    // Let's make another access and eviction to verify further
    std::cout << "Accessing Page 2 to set its RefBit back to 1...\n";
    cache.access(2);
    cache.displayBuffer();

    std::cout << "Inserting Page 5 (causes eviction of Key 3 or 4 depending on hand & RefBits)\n";
    // Hand is currently at 1 (Key 2, RefBit 1):
    // - Frame 1 has RefBit 1 -> resets to 0, hand moves to 2
    // - Frame 2 (Key 3, RefBit 0) -> evicted! PageE inserted into Frame 2.
    cache.insert(5, "PageE");
    cache.displayBuffer();

    std::cout << "========================================================\n";
    std::cout << "  Lab 3 Demo Completed Successfully!                    \n";
    std::cout << "========================================================\n";

    return 0;
}