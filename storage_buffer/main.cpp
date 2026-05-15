#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

template <typename K, typename V>
class ClockSweep {
public:
    explicit ClockSweep(std::size_t maxNumber) : maxCacheSize(maxNumber) {
        slots.reserve(maxCacheSize);
    }

    std::optional<V> getKey(const K& key) {
        std::lock_guard<std::mutex> lock(mu);
        auto it = index.find(key);
        if (it == index.end()) return std::nullopt;
        Slot& s = slots[it->second];
        s.ref = true;
        return s.value;
    }

    void putKey(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mu);

        auto it = index.find(key);
        if (it != index.end()) {
            Slot& s = slots[it->second];
            s.value = value;
            s.ref = true;
            return;
        }

        if (slots.size() < maxCacheSize) {
            std::size_t idx = slots.size();
            slots.push_back({key, value, true});
            index[key] = idx;
            return;
        }

        std::size_t victim = sweep();
        Slot& s = slots[victim];
        index.erase(s.key);
        s.key = key;
        s.value = value;
        s.ref = true;
        index[key] = victim;
    }

private:
    struct Slot {
        K key;
        V value;
        bool ref;
    };

    std::size_t sweep() {
        while (true) {
            Slot& s = slots[hand];
            if (!s.ref) {
                std::size_t victim = hand;
                hand = (hand + 1) % maxCacheSize;
                return victim;
            }
            s.ref = false;
            hand = (hand + 1) % maxCacheSize;
        }
    }

    std::size_t maxCacheSize{0};
    std::size_t hand{0};
    std::vector<Slot> slots;
    std::unordered_map<K, std::size_t> index;
    std::mutex mu;
};

int main() {
    ClockSweep<int, std::string> cache(3);

    cache.putKey(1, "one");
    cache.putKey(2, "two");
    cache.putKey(3, "three");

    cache.getKey(1);
    cache.getKey(3);

    cache.putKey(4, "four");

    for (int k : {1, 2, 3, 4}) {
        auto v = cache.getKey(k);
        std::cout << k << " -> " << (v ? *v : std::string("MISS")) << "\n";
    }
}
