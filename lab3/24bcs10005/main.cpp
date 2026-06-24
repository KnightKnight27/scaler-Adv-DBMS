#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

template <typename T>
class ClockSweep {
public:
    explicit ClockSweep(
        std::size_t capacity,
        std::chrono::milliseconds sweepIntervalMs =
            std::chrono::milliseconds(500)
    )
        : capacity_(capacity),
          sweepInterval_(sweepIntervalMs),
          buffer_(capacity),
          clockHand_(0),
          running_(true) {

        if (capacity == 0) {
            throw std::invalid_argument(
                "Cache capacity must be greater than 0"
            );
        }

        sweeper_ = std::thread(
            &ClockSweep::backgroundSweep,
            this
        );
    }

    ~ClockSweep() {
        {
            std::lock_guard<std::mutex> guard(mtx_);
            running_ = false;
        }

        wakeup_.notify_all();

        if (sweeper_.joinable()) {
            sweeper_.join();
        }
    }

    ClockSweep(const ClockSweep&) = delete;
    ClockSweep& operator=(const ClockSweep&) = delete;

    T getKey(const T& key) {
        std::lock_guard<std::mutex> guard(mtx_);

        auto found = lookup_.find(key);

        if (found == lookup_.end()) {
            return T{};
        }

        buffer_[found->second].referenced = true;

        return buffer_[found->second].data;
    }

    void putKey(const T& key) {
        std::lock_guard<std::mutex> guard(mtx_);

        auto found = lookup_.find(key);

        if (found != lookup_.end()) {
            buffer_[found->second].referenced = true;
            return;
        }

        for (std::size_t i = 0; i < buffer_.size(); ++i) {
            std::size_t idx =
                (clockHand_ + i) % buffer_.size();

            if (!buffer_[idx].valid) {
                buffer_[idx] =
                    Frame{key, true, true};

                lookup_[key] = idx;

                clockHand_ =
                    (idx + 1) % buffer_.size();

                return;
            }
        }

        std::size_t victim = evictVictim();

        lookup_.erase(buffer_[victim].data);

        buffer_[victim] =
            Frame{key, true, true};

        lookup_[key] = victim;

        clockHand_ =
            (victim + 1) % buffer_.size();
    }

    bool contains(const T& key) {
        std::lock_guard<std::mutex> guard(mtx_);

        return lookup_.count(key) > 0;
    }

    std::size_t size() {
        std::lock_guard<std::mutex> guard(mtx_);

        return lookup_.size();
    }

    std::size_t capacity() const {
        return capacity_;
    }

    void dump(const std::string& tag) {
        std::lock_guard<std::mutex> guard(mtx_);

        std::cout << "[" << tag << "] hand="
                  << clockHand_ << " |";

        for (std::size_t i = 0;
             i < buffer_.size();
             ++i) {

            std::cout << " f" << i << "=";

            if (buffer_[i].valid) {
                std::cout
                    << buffer_[i].data
                    << "(r="
                    << (buffer_[i].referenced
                            ? 1
                            : 0)
                    << ")";
            } else {
                std::cout << "empty";
            }
        }

        std::cout << "\n";
    }

private:
    struct Frame {
        T data{};
        bool referenced{false};
        bool valid{false};
    };

    std::size_t capacity_;

    std::chrono::milliseconds sweepInterval_;

    std::vector<Frame> buffer_;

    std::unordered_map<T, std::size_t> lookup_;

    std::size_t clockHand_;

    std::mutex mtx_;

    std::condition_variable wakeup_;

    bool running_;

    std::thread sweeper_;

    std::size_t evictVictim() {

        for (std::size_t n = 0;
             n < 2 * buffer_.size();
             ++n) {

            Frame& f = buffer_[clockHand_];

            if (f.valid && !f.referenced) {
                return clockHand_;
            }

            if (f.valid && f.referenced) {
                f.referenced = false;
            }

            clockHand_ =
                (clockHand_ + 1) %
                buffer_.size();
        }

        return clockHand_;
    }

    void backgroundSweep() {
        std::unique_lock<std::mutex> lock(mtx_);

        while (running_) {

            if (wakeup_.wait_for(
                    lock,
                    sweepInterval_,
                    [this] {
                        return !running_;
                    }
                )) {
                break;
            }

            for (auto& f : buffer_) {

                if (f.valid && f.referenced) {
                    f.referenced = false;
                }
            }
        }
    }
};

namespace {

void demo_integer_cache() {

    std::cout
        << "=== Integer Cache Demo ===\n";

    ClockSweep<int> cache(
        4,
        std::chrono::milliseconds(300)
    );

    cache.putKey(10);
    cache.dump("put 10");

    cache.putKey(20);
    cache.dump("put 20");

    cache.putKey(30);
    cache.dump("put 30");

    cache.putKey(40);
    cache.dump("put 40");

    std::this_thread::sleep_for(
        std::chrono::milliseconds(400)
    );

    cache.dump("after sweep");

    cache.getKey(20);
    cache.getKey(40);

    cache.dump("accessed 20 and 40");

    cache.putKey(50);
    cache.dump("put 50");

    cache.putKey(60);
    cache.dump("put 60");

    std::cout
        << "contains(20): "
        << std::boolalpha
        << cache.contains(20)
        << "\n";

    std::cout
        << "contains(10): "
        << cache.contains(10)
        << "\n";

    std::cout
        << "size: "
        << cache.size()
        << "\n\n";
}

void demo_string_cache() {

    std::cout
        << "=== String Cache Demo ===\n";

    ClockSweep<std::string> cache(
        3,
        std::chrono::milliseconds(500)
    );

    cache.putKey("red");
    cache.putKey("green");
    cache.putKey("blue");

    cache.dump("filled");

    cache.getKey("red");

    std::this_thread::sleep_for(
        std::chrono::milliseconds(600)
    );

    cache.dump("after sweep");

    cache.putKey("yellow");

    cache.dump("put yellow");

    cache.putKey("purple");

    cache.dump("put purple");

    std::cout
        << "contains(red): "
        << cache.contains("red")
        << "\n";

    std::cout
        << "contains(green): "
        << cache.contains("green")
        << "\n\n";
}

void demo_duplicate_put() {

    std::cout
        << "=== Duplicate Entry Demo ===\n";

    ClockSweep<int> cache(
        3,
        std::chrono::milliseconds(1000)
    );

    cache.putKey(100);
    cache.putKey(200);
    cache.putKey(300);

    cache.dump("filled");

    std::this_thread::sleep_for(
        std::chrono::milliseconds(1100)
    );

    cache.dump("after sweep");

    cache.putKey(200);

    cache.dump("put 200 again");

    std::cout
        << "size: "
        << cache.size()
        << "\n\n";
}

}

int main() {

    demo_integer_cache();

    demo_string_cache();

    demo_duplicate_put();

    std::cout
        << "All demos completed.\n";

    return 0;
}