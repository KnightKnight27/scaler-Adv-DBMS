#include <iostream>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>

template<typename T>
class CacheClock {
private:
    static constexpr uint8_t LIMIT = 5;

    struct Entry {
        T value{};
        uint8_t counter{0};
        bool locked{false};
        bool active{false};
    };

    std::vector<Entry> storage;
    std::unordered_map<T, size_t> lookup;
    std::mutex guard;

    size_t pointer = 0;
    size_t capacity;

    size_t locateEmpty() {
        for (size_t i = 0; i < capacity; ++i) {
            if (!storage[i].active)
                return i;
        }
        return capacity;
    }

    size_t selectVictim() {
        size_t origin = pointer;
        size_t rounds = 0;

        while (rounds <= LIMIT) {
            size_t current = pointer;
            pointer = (pointer + 1) % capacity;

            auto &slot = storage[current];

            if (!slot.active)
                return current;

            if (slot.locked) {
                if (pointer == origin) rounds++;
                continue;
            }

            if (slot.counter == 0) {
                lookup.erase(slot.value);

                slot.active = false;
                slot.locked = false;
                slot.counter = 0;

                return current;
            }

            slot.counter--;

            if (pointer == origin)
                rounds++;
        }

        return capacity;
    }

public:
    explicit CacheClock(size_t size = 16384)
        : capacity(size), storage(size) {}

    std::optional<T> fetch(T key) {
        std::lock_guard<std::mutex> lock(guard);

        auto itr = lookup.find(key);
        if (itr == lookup.end())
            return std::nullopt;

        auto &slot = storage[itr->second];

        slot.locked = true;
        if (slot.counter < LIMIT)
            slot.counter++;

        return slot.value;
    }

    void insert(T key) {
        std::lock_guard<std::mutex> lock(guard);

        auto itr = lookup.find(key);

        if (itr != lookup.end()) {
            auto &slot = storage[itr->second];

            slot.locked = true;
            if (slot.counter < LIMIT)
                slot.counter++;

            return;
        }

        size_t indexPos = locateEmpty();

        if (indexPos == capacity) {
            indexPos = selectVictim();

            if (indexPos == capacity) {
                std::cerr << "No available frame: all entries are locked.\n";
                return;
            }
        }

        auto &slot = storage[indexPos];

        slot.value = key;
        slot.counter = 1;
        slot.locked = true;
        slot.active = true;

        lookup[key] = indexPos;
    }

    void unlock(T key) {
        std::lock_guard<std::mutex> lock(guard);

        auto itr = lookup.find(key);
        if (itr != lookup.end()) {
            storage[itr->second].locked = false;
        }
    }

    void display() {
        std::lock_guard<std::mutex> lock(guard);

        std::cout << "=== Cache Snapshot ===\n";
        std::cout << "Clock Pointer: " << pointer << "\n";

        for (size_t i = 0; i < capacity; ++i) {
            const auto &slot = storage[i];

            if (!slot.active)
                continue;

            std::cout << "[Index " << i << "] "
                      << "Value=" << slot.value
                      << ", Count=" << static_cast<int>(slot.counter)
                      << ", Locked=" << (slot.locked ? "Yes" : "No")
                      << '\n';
        }
    }
};

int main() {
    CacheClock<int> cache(5);

    cache.insert(10);
    cache.insert(20);
    cache.insert(30);

    cache.unlock(10);
    cache.unlock(20);
    cache.unlock(30);

    cache.display();

    cache.insert(40);
    cache.insert(50);

    cache.unlock(40);
    cache.unlock(50);

    cache.insert(60);

    std::cout << "\nAfter replacement:\n";
    cache.display();

    return 0;
}