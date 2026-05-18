#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <optional>

template<typename T>
class BufferPool {
public:
    struct PageRecord {
        T pageId;
        int pinCount;
    };

    BufferPool(int capacity)
        : poolSize(capacity), shouldStop(false)
    {
        sweepThread = std::thread(&BufferPool::clockSweeper, this);
    }

    ~BufferPool() {
        shouldStop = true;

        if (sweepThread.joinable()) {
            sweepThread.join();
        }
    }

    std::optional<PageRecord> fetchPage(const T& pageId) {
        std::lock_guard<std::mutex> lock(poolMutex);

        auto it = pool.find(pageId);

        if (it != pool.end()) {
            std::cout << "Page found: " << pageId
                      << ", PinCount = "
                      << it->second.pinCount << "\n";

            return it->second;
        }

        std::cout << "Page not found\n";
        return std::nullopt;
    }

    void loadPage(const T& pageId) {
        std::lock_guard<std::mutex> lock(poolMutex);

        if (pool.find(pageId) != pool.end()) {
            std::cout << "Page already in pool\n";
            return;
        }

        if (pool.size() < poolSize) {
            pool[pageId] = {pageId, 5};

            std::cout << "Loaded page: " << pageId << "\n";
            return;
        }

        auto victimIt = pool.begin();

        for (auto it = pool.begin(); it != pool.end(); ++it) {
            if (it->second.pinCount < victimIt->second.pinCount) {
                victimIt = it;
            }
        }

        std::cout << "Evicting page: "
                  << victimIt->first
                  << " with pinCount "
                  << victimIt->second.pinCount
                  << "\n";

        pool.erase(victimIt);

        pool[pageId] = {pageId, 5};

        std::cout << "Loaded page: " << pageId << "\n";
    }

    void printPool() {
        std::lock_guard<std::mutex> lock(poolMutex);

        std::cout << "\nCurrent Buffer Pool:\n";

        for (auto& [pageId, record] : pool) {
            std::cout << "Page = "
                      << pageId
                      << ", PinCount = "
                      << record.pinCount
                      << "\n";
        }

        std::cout << "\n";
    }

private:
    void clockSweeper() {
        while (!shouldStop) {

            std::this_thread::sleep_for(std::chrono::seconds(1));

            std::lock_guard<std::mutex> lock(poolMutex);

            for (auto& [pageId, record] : pool) {

                if (record.pinCount > 0) {
                    record.pinCount--;
                }
            }
        }
    }

private:
    size_t poolSize{0u};

    std::unordered_map<T, PageRecord> pool;

    std::thread sweepThread;

    std::mutex poolMutex;

    bool shouldStop;
};

int main() {

    BufferPool<int> bufferPool(3);

    bufferPool.loadPage(10);
    bufferPool.loadPage(20);
    bufferPool.loadPage(30);

    bufferPool.printPool();

    std::this_thread::sleep_for(std::chrono::seconds(3));

    bufferPool.fetchPage(10);

    bufferPool.printPool();

    bufferPool.loadPage(40);

    bufferPool.printPool();

    return 0;
}
