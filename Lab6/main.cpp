#include <vector>
#include <unordered_map>
#include <optional>
#include <cstdio>
#include <string>

template <typename K, typename V>
class ClockSweep {
public:
    explicit ClockSweep(size_t maxNumber)
        : maxCacheSize(maxNumber), slots(maxNumber) {}

    std::optional<V> get(const K& key) {
        auto it = index.find(key);
        if (it == index.end()) return std::nullopt;
        slots[it->second].refBit = true;
        return slots[it->second].value;
    }

    void put(const K& key, const V& value) {
        auto it = index.find(key);
        if (it != index.end()) {
            slots[it->second].value = value;
            slots[it->second].refBit = true;
            return;
        }

        size_t slotIdx;
        if (filled < maxCacheSize) {
            slotIdx = filled++;
        } else {
            while (slots[hand].refBit) {
                slots[hand].refBit = false;
                hand = (hand + 1) % maxCacheSize;
            }
            slotIdx = hand;
            index.erase(slots[hand].key);
            printf("evicting key %d to make room\n", slots[hand].key);
            hand = (hand + 1) % maxCacheSize;
        }

        slots[slotIdx].key = key;
        slots[slotIdx].value = value;
        slots[slotIdx].refBit = true;
        index[key] = slotIdx;
    }

private:
    struct Slot {
        K key{};
        V value{};
        bool refBit{false};
    };

    size_t maxCacheSize;
    std::vector<Slot> slots;
    std::unordered_map<K, size_t> index;
    size_t hand{0};
    size_t filled{0};
};

int main() {
    ClockSweep<int, std::string> cache(3);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    if (auto v = cache.get(1)) printf("get(1) = %s\n", v->c_str());

    cache.put(4, "four");

    printf("get(1) = %s\n", cache.get(1) ? "hit" : "miss");
    printf("get(2) = %s\n", cache.get(2) ? "hit" : "miss");
    printf("get(3) = %s\n", cache.get(3) ? "hit" : "miss");
    printf("get(4) = %s\n", cache.get(4) ? "hit" : "miss");

    return 0;
}