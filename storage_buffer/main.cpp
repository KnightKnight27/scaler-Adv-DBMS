#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>

template <typename Key>
class ClockCache {
public:
    explicit ClockCache(size_t capacity)
        : slots_(NormalizeCapacity(capacity)),
          pointer_(0),
          filled_(0),
          mask_(slots_.empty() ? 0 : slots_.size() - 1) {}

    bool Access(const Key& key) {
        auto pos = index_map_.find(key);

        if (pos == index_map_.end()) {
            return false;
        }

        Entry& entry = slots_[pos->second];

        if (entry.reference_count < kReferenceLimit) {
            ++entry.reference_count;
        }

        return true;
    }

    void Insert(const Key& key) {
        if (slots_.empty()) {
            return;
        }

        auto existing = index_map_.find(key);

        if (existing != index_map_.end()) {
            Entry& entry = slots_[existing->second];

            if (entry.reference_count < kReferenceLimit) {
                ++entry.reference_count;
            }

            return;
        }

        if (filled_ < slots_.size()) {
            PlaceEntry(filled_, key);
            ++filled_;
            return;
        }

        const size_t total_slots = slots_.size();
        const size_t max_attempts = total_slots * (kReferenceLimit + 1);

        for (size_t attempt = 0; attempt < max_attempts; ++attempt) {
            Entry& current = slots_[pointer_];

            if (current.reference_count > 0) {
                --current.reference_count;
                MovePointer();
                continue;
            }

            RemoveEntry(pointer_);
            PlaceEntry(pointer_, key);
            MovePointer();
            return;
        }
    }

    void PrintState() const {
        for (const auto& entry : slots_) {
            if (entry.active) {
                std::cout << "["
                          << entry.key
                          << " ref=" << static_cast<int>(entry.reference_count)
                          << "] ";
            } else {
                std::cout << "[empty] ";
            }
        }

        std::cout << "pointer=" << pointer_ << '\n';
    }

private:
    struct Entry {
        Key key{};
        uint8_t reference_count{0};
        bool active{false};
    };

    static constexpr uint8_t kReferenceLimit = 5;

    static size_t NormalizeCapacity(size_t value) {
        if (value == 0) {
            return 0;
        }

        --value;

        for (size_t shift = 1; shift < sizeof(size_t) * 8; shift <<= 1) {
            value |= (value >> shift);
        }

        return value + 1;
    }

    void MovePointer() {
        pointer_ = (pointer_ + 1) & mask_;
    }

    void RemoveEntry(size_t index) {
        Entry& victim = slots_[index];

        if (!victim.active) {
            return;
        }

        index_map_.erase(victim.key);

        victim.active = false;
        victim.reference_count = 0;
    }

    void PlaceEntry(size_t index, const Key& key) {
        Entry& target = slots_[index];

        target.key = key;
        target.reference_count = 1;
        target.active = true;

        index_map_[key] = index;
    }

private:
    std::vector<Entry> slots_;
    std::unordered_map<Key, size_t> index_map_;

    size_t pointer_;
    size_t filled_;
    size_t mask_;
};

int main() {
    ClockCache<int> cache(3);

    cache.Insert(10);
    cache.Insert(20);
    cache.Insert(30);

    cache.Access(10);
    cache.Access(10);
    cache.Access(20);

    std::cout << "Cache before replacement:\n";
    cache.PrintState();

    cache.Insert(40);

    std::cout << "Cache after inserting 40:\n";
    cache.PrintState();

    return 0;
}