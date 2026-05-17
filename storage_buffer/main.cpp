#include <cstdio>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

template <typename K, typename V>
class ClockSweep {
public:
    explicit ClockSweep(size_t capacity)
        : capacity_(capacity), frames_(capacity) {}

    std::optional<V> get(const K& key) {
        auto it = lookup_.find(key);
        if (it == lookup_.end()) return std::nullopt;
        frames_[it->second].referenced = true;
        return frames_[it->second].value;
    }

    void put(const K& key, const V& value) {
        if (auto it = lookup_.find(key); it != lookup_.end()) {
            frames_[it->second].value = value;
            frames_[it->second].referenced = true;
            return;
        }

        size_t target;
        if (used_ < capacity_) {
            target = used_++;
        } else {
            target = sweep();
            lookup_.erase(frames_[target].key);
            printf("evicting key %d from frame %zu\n", frames_[target].key, target);
        }

        frames_[target].key = key;
        frames_[target].value = value;
        frames_[target].referenced = true;
        lookup_[key] = target;
    }

private:
    struct Frame {
        K key{};
        V value{};
        bool referenced{false};
    };

    size_t sweep() {
        while (frames_[hand_].referenced) {
            frames_[hand_].referenced = false;
            hand_ = (hand_ + 1) % capacity_;
        }
        size_t victim = hand_;
        hand_ = (hand_ + 1) % capacity_;
        return victim;
    }

    size_t capacity_;
    std::vector<Frame> frames_;
    std::unordered_map<K, size_t> lookup_;
    size_t hand_{0};
    size_t used_{0};
};

int main() {
    ClockSweep<int, std::string> cache(3);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    if (auto v = cache.get(1)) printf("get(1) -> %s\n", v->c_str());

    cache.put(4, "four");

    printf("get(1) -> %s\n", cache.get(1) ? "hit" : "miss");
    printf("get(2) -> %s\n", cache.get(2) ? "hit" : "miss");
    printf("get(3) -> %s\n", cache.get(3) ? "hit" : "miss");
    printf("get(4) -> %s\n", cache.get(4) ? "hit" : "miss");

    return 0;
}
