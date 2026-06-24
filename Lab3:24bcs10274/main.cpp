#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

template<typename T>
class ClockCache {
private:

    struct Slot {
        T value{};
        bool occupied = false;
        bool refBit = false;
    };

    std::vector<Slot> pages_;
    std::unordered_map<T, size_t> indexMap_;

    size_t capacity_;
    size_t hand_;

    std::mutex lock_;
    std::condition_variable cv_;

    bool stopWorker_;
    std::thread worker_;

    std::chrono::milliseconds interval_;

private:

    size_t findVictim() {

        while (true) {

            Slot& current = pages_[hand_];

            if (!current.refBit) {
                return hand_;
            }

            current.refBit = false;

            hand_ = (hand_ + 1) % capacity_;
        }
    }

    void agingWorker() {

        std::unique_lock<std::mutex> guard(lock_);

        while (!stopWorker_) {

            if (cv_.wait_for(
                    guard,
                    interval_,
                    [this]() { return stopWorker_; }))
            {
                break;
            }

            for (auto& slot : pages_) {
                if (slot.occupied) {
                    slot.refBit = false;
                }
            }
        }
    }

public:

    ClockCache(
        size_t capacity,
        std::chrono::milliseconds interval =
            std::chrono::milliseconds(500))
        :
          pages_(capacity),
          capacity_(capacity),
          hand_(0),
          stopWorker_(false),
          interval_(interval)
    {

        if (capacity == 0) {
            throw std::runtime_error("Capacity cannot be zero");
        }

        worker_ = std::thread(&ClockCache::agingWorker, this);
    }

    ~ClockCache() {

        {
            std::lock_guard<std::mutex> guard(lock_);
            stopWorker_ = true;
        }

        cv_.notify_all();

        if (worker_.joinable()) {
            worker_.join();
        }
    }

    ClockCache(const ClockCache&) = delete;
    ClockCache& operator=(const ClockCache&) = delete;

    void insert(const T& value) {

        std::lock_guard<std::mutex> guard(lock_);

        auto found = indexMap_.find(value);

        if (found != indexMap_.end()) {

            pages_[found->second].refBit = true;
            return;
        }

        for (size_t i = 0; i < capacity_; ++i) {

            if (!pages_[i].occupied) {

                pages_[i].value = value;
                pages_[i].occupied = true;
                pages_[i].refBit = true;

                indexMap_[value] = i;

                return;
            }
        }

        size_t victim = findVictim();

        indexMap_.erase(pages_[victim].value);

        pages_[victim].value = value;
        pages_[victim].refBit = true;
        pages_[victim].occupied = true;

        indexMap_[value] = victim;

        hand_ = (victim + 1) % capacity_;
    }

    T fetch(const T& value) {

        std::lock_guard<std::mutex> guard(lock_);

        auto found = indexMap_.find(value);

        if (found == indexMap_.end()) {
            return T{};
        }

        pages_[found->second].refBit = true;

        return pages_[found->second].value;
    }

    bool exists(const T& value) {

        std::lock_guard<std::mutex> guard(lock_);

        return indexMap_.count(value) > 0;
    }

    size_t currentSize() {

        std::lock_guard<std::mutex> guard(lock_);

        return indexMap_.size();
    }

    void printState(const std::string& label) {

        std::lock_guard<std::mutex> guard(lock_);

        std::cout << "\n[" << label << "] ";

        std::cout << "hand=" << hand_ << " -> ";

        for (size_t i = 0; i < capacity_; ++i) {

            if (pages_[i].occupied) {

                std::cout
                    << pages_[i].value
                    << "(ref="
                    << pages_[i].refBit
                    << ") ";
            }
            else {
                std::cout << "empty ";
            }
        }

        std::cout << "\n";
    }
};

void integerDemo() {

    std::cout << "\n===== INTEGER CACHE =====\n";

    ClockCache<int> cache(
        4,
        std::chrono::milliseconds(300));

    cache.insert(10);
    cache.insert(20);
    cache.insert(30);
    cache.insert(40);

    cache.printState("Initial Fill");

    std::this_thread::sleep_for(
        std::chrono::milliseconds(500));

    cache.printState("After Aging");

    cache.fetch(20);
    cache.fetch(40);

    cache.printState("Touched 20 and 40");

    cache.insert(50);

    cache.printState("Inserted 50");

    cache.insert(60);

    cache.printState("Inserted 60");

    std::cout << "\n20 exists: "
              << cache.exists(20);

    std::cout << "\n10 exists: "
              << cache.exists(10);

    std::cout << "\nCurrent size: "
              << cache.currentSize()
              << "\n";
}

void stringDemo() {

    std::cout << "\n===== STRING CACHE =====\n";

    ClockCache<std::string> cache(
        3,
        std::chrono::milliseconds(400));

    cache.insert("apple");
    cache.insert("banana");
    cache.insert("orange");

    cache.printState("Initial");

    cache.fetch("banana");

    std::this_thread::sleep_for(
        std::chrono::milliseconds(600));

    cache.insert("grape");

    cache.printState("After inserting grape");

    std::cout << "\nbanana exists: "
              << cache.exists("banana");

    std::cout << "\napple exists: "
              << cache.exists("apple")
              << "\n";
}

int main() {

    integerDemo();

    stringDemo();

    std::cout << "\nProgram Finished\n";

    return 0;
}
