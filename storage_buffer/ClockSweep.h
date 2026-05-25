#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

template <typename T>
class ClockSweep {
public:
    explicit ClockSweep(std::size_t max_cache_size,
                        std::chrono::milliseconds sweep_interval =
                            std::chrono::milliseconds(250))
        : max_cache_size_(max_cache_size),
          frames_(max_cache_size),
          sweep_interval_(sweep_interval) {
        if (max_cache_size_ == 0) {
            throw std::invalid_argument("max_cache_size must be greater than zero");
        }

        bg_clock_thread_ = std::thread(&ClockSweep::runBackgroundSweep, this);
    }

    ~ClockSweep() {
        stop_background_thread_.store(true);
        if (bg_clock_thread_.joinable()) {
            bg_clock_thread_.join();
        }
    }

    std::optional<T> get(const T& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto position = page_table_.find(key);
        if (position == page_table_.end()) {
            return std::nullopt;
        }

        Frame& frame = frames_[position->second];
        frame.referenced = true;
        return frame.value;
    }

    void put(const T& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto existing = page_table_.find(key);
        if (existing != page_table_.end()) {
            frames_[existing->second].referenced = true;
            return;
        }

        if (size_ < max_cache_size_) {
            insertIntoFrame(firstFreeFrame(), key);
            return;
        }

        insertIntoFrame(findVictimFrame(), key);
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    void sweepOnce() {
        std::lock_guard<std::mutex> lock(mutex_);
        advanceClockHand();
    }

private:
    struct Frame {
        std::optional<T> value;
        bool referenced{false};
        bool valid{false};
    };

    std::size_t firstFreeFrame() const {
        for (std::size_t index = 0; index < frames_.size(); ++index) {
            if (!frames_[index].valid) {
                return index;
            }
        }

        throw std::logic_error("no free frame available");
    }

    void insertIntoFrame(std::size_t index, const T& key) {
        Frame& frame = frames_[index];
        if (frame.valid && frame.value.has_value()) {
            page_table_.erase(*frame.value);
        }

        frame.value = key;
        frame.referenced = true;
        frame.valid = true;
        page_table_[key] = index;

        if (size_ < max_cache_size_) {
            ++size_;
        }
    }

    std::size_t findVictimFrame() {
        while (true) {
            Frame& frame = frames_[clock_hand_];
            if (!frame.valid) {
                const std::size_t free_index = clock_hand_;
                clock_hand_ = (clock_hand_ + 1) % max_cache_size_;
                return free_index;
            }

            if (frame.referenced) {
                frame.referenced = false;
            } else {
                const std::size_t victim_index = clock_hand_;
                clock_hand_ = (clock_hand_ + 1) % max_cache_size_;
                return victim_index;
            }

            clock_hand_ = (clock_hand_ + 1) % max_cache_size_;
        }
    }

    void advanceClockHand() {
        if (size_ == 0) {
            return;
        }

        Frame& frame = frames_[clock_hand_];
        if (frame.valid && frame.referenced) {
            frame.referenced = false;
        }

        clock_hand_ = (clock_hand_ + 1) % max_cache_size_;
    }

    void runBackgroundSweep() {
        while (!stop_background_thread_.load()) {
            std::this_thread::sleep_for(sweep_interval_);
            sweepOnce();
        }
    }

    const std::size_t max_cache_size_;
    mutable std::mutex mutex_;
    std::unordered_map<T, std::size_t> page_table_;
    std::vector<Frame> frames_;
    std::size_t size_{0};
    std::size_t clock_hand_{0};
    std::chrono::milliseconds sweep_interval_;
    std::atomic<bool> stop_background_thread_{false};
    std::thread bg_clock_thread_;
};
