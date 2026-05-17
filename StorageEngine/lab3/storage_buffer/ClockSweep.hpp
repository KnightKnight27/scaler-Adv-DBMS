#pragma once

#include <vector>
#include <unordered_map>
#include <optional>
#include <cstdint>
#include <cstddef>
#include <iostream>

template <typename K, typename V>
class ClockSweep {
public:
    explicit ClockSweep(size_t capacity, uint8_t max_usage = 5)
        : capacity_(capacity),
          max_usage_(max_usage),
          frames_(capacity),
          hand_(0) {}

    std::optional<V> get(const K& key) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return std::nullopt;
        }
        size_t slot = it->second;
        bump_usage(slot);
        return frames_[slot].value;
    }

    void put(const K& key, const V& value) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            size_t slot = it->second;
            frames_[slot].value = value;
            bump_usage(slot);
            return;
        }

        if (index_.size() < capacity_) {
            for (size_t i = 0; i < capacity_; ++i) {
                if (!frames_[i].valid) {
                    install(i, key, value);
                    return;
                }
            }
        }

        size_t victim = sweep_for_victim();
        index_.erase(frames_[victim].key);
        install(victim, key, value);
        hand_ = (victim + 1) % capacity_;
    }

    void dump() const {
        std::cout << "[hand=" << hand_ << "] ";
        for (size_t i = 0; i < capacity_; ++i) {
            if (frames_[i].valid) {
                std::cout << "[" << frames_[i].key << ":"
                          << int(frames_[i].usage) << "] ";
            } else {
                std::cout << "[ - ] ";
            }
        }
        std::cout << "\n";
    }

private:
    struct Frame {
        K key{};
        V value{};
        uint8_t usage{0};
        bool valid{false};
    };

    size_t capacity_;
    uint8_t max_usage_;
    std::vector<Frame> frames_;
    std::unordered_map<K, size_t> index_;
    size_t hand_;

    void bump_usage(size_t slot) {
        if (frames_[slot].usage < max_usage_) {
            frames_[slot].usage++;
        }
    }

    void install(size_t slot, const K& key, const V& value) {
        frames_[slot].key = key;
        frames_[slot].value = value;
        frames_[slot].usage = 1;
        frames_[slot].valid = true;
        index_[key] = slot;
    }

    size_t sweep_for_victim() {
        while (true) {
            Frame& f = frames_[hand_];
            if (f.usage == 0) {
                return hand_;
            }
            f.usage--;
            hand_ = (hand_ + 1) % capacity_;
        }
    }
};
