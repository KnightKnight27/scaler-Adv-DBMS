#include <iostream>
#include <vector>
#include <unordered_map>
#include <stdexcept>

template <typename Key, typename Value>
class ClockSweepCache {
private:
    struct Slot {
        Key key{};
        Value value{};
        int refCount{0};
        bool occupied{false};
    };

public:
    explicit ClockSweepCache(std::size_t capacity)
        : capacity_(capacity), hand_(0), slots_(capacity)
    {
        if (capacity_ == 0) {
            throw std::invalid_argument("Cache capacity must be greater than zero");
        }
    }

    void put(const Key& key, const Value& value) {
        auto it = position_.find(key);
        if (it != position_.end()) {
            std::size_t idx = it->second;
            slots_[idx].value = value;
            slots_[idx].refCount = 1;
            return;
        }

        while (true) {
            Slot& current = slots_[hand_];

            if (!current.occupied) {
                placeAtHand(key, value);
                return;
            }

            if (current.refCount == 0) {
                position_.erase(current.key);
                placeAtHand(key, value);
                return;
            }

            current.refCount--;
            advanceHand();
        }
    }

    bool get(const Key& key, Value& out) {
        auto it = position_.find(key);
        if (it == position_.end()) {
            return false;
        }

        std::size_t idx = it->second;
        slots_[idx].refCount++;
        out = slots_[idx].value;
        return true;
    }

    void print() const {
        std::cout << "\nCache State\n";
        for (std::size_t i = 0; i < capacity_; ++i) {
            const Slot& s = slots_[i];

            std::cout << "[" << i << "] ";
            if (s.occupied) {
                std::cout << "Key=" << s.key
                          << " Value=" << s.value
                          << " Ref=" << s.refCount;
            } else {
                std::cout << "EMPTY";
            }

            if (i == hand_) {
                std::cout << " <-- hand";
            }

            std::cout << '\n';
        }
    }

private:
    void advanceHand() {
        hand_ = (hand_ + 1) % capacity_;
    }

    void placeAtHand(const Key& key, const Value& value) {
        slots_[hand_].key = key;
        slots_[hand_].value = value;
        slots_[hand_].refCount = 1;
        slots_[hand_].occupied = true;
        position_[key] = hand_;
        advanceHand();
    }

private:
    std::size_t capacity_;
    std::size_t hand_;
    std::vector<Slot> slots_;
    std::unordered_map<Key, std::size_t> position_;
};

int main() {
    ClockSweepCache<int, int> cache(3);

    cache.put(8, 100);
    cache.put(5, 200);
    cache.put(2, 300);

    cache.print();

    int value{};
    if (cache.get(5, value)) {
        std::cout << "\nFound value: " << value << '\n';
    } else {
        std::cout << "\nKey not found\n";
    }

    cache.put(5, 400);
    cache.put(9, 900);

    cache.print();

    return 0;
}
