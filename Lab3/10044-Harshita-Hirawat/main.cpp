#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <cstdint>

template<typename T>
class ClockSweep {
public:
    ClockSweep(int maxNumber): maxCacheSize(maxNumber), clockHand(0), stopThread(false) {
        bgClockThread = std::thread(&ClockSweep::runClock, this);
    }

    ~ClockSweep() {
        stopThread = true;

        if(bgClockThread.joinable()) {
            bgClockThread.join();
        }
    }

    T getKey(T key) {
        std::lock_guard<std::mutex> lock(mtx);

        if(cacheMap.find(key) != cacheMap.end()) {
            int index = cacheMap[key];

            pages[index].usageCount++;
            pages[index].pinned = true;

            std::cout << "Key Found: " << key << std::endl;

            return pages[index].key;
        }

        std::cout << "Key Not Found: " << key << std::endl;
        return T();
    }

    void putKey(T key) {
        std::lock_guard<std::mutex> lock(mtx);

        // key already exists
        if(cacheMap.find(key) != cacheMap.end()) {
            int index = cacheMap[key];

            pages[index].usageCount++;
            pages[index].pinned = true;

            std::cout << "Key Already Present: " << key << std::endl;
            return;
        }

        // cache not full
        if(pages.size() < maxCacheSize) {
            Page newPage;

            newPage.key = key;
            newPage.usageCount = 1;
            newPage.pinned = true;

            pages.push_back(newPage);

            cacheMap[key] = pages.size() - 1;

            std::cout << "Inserted Key: " << key << std::endl;
        }
        else {
            evictAndInsert(key);
        }
    }

private:
    struct Page {
        T key;
        int usageCount{0};
        bool pinned{false};
    };

    uint32_t maxCacheSize = 0;
    std::vector<Page> pages;
    std::unordered_map<T, int> cacheMap;
    std::thread bgClockThread;
    std::mutex mtx;
    size_t clockHand{0};
    bool stopThread{false};

    void evictAndInsert(T key) {
        while(true) {
            if(clockHand >= pages.size()) {
                clockHand = 0;
            }

            // if page recently used
            if(pages[clockHand].usageCount > 0) {
                pages[clockHand].usageCount--;
            }
            else if(!pages[clockHand].pinned) {
                T oldKey = pages[clockHand].key;

                cacheMap.erase(oldKey);

                pages[clockHand].key = key;
                pages[clockHand].usageCount = 1;
                pages[clockHand].pinned = true;

                cacheMap[key] = clockHand;

                std::cout << "Evicted Key: " << oldKey << " , Inserted Key: " << key << std::endl;

                clockHand++;
                break;
            }

            clockHand++;
        }
    }

    void runClock() {
        while(!stopThread) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::lock_guard<std::mutex> lock(mtx);

            for(auto &page : pages) {
                // unpin pages after some time
                if(page.pinned) {
                    page.pinned = false;
                }

                // reduce usage count gradually
                if(page.usageCount > 0) {
                    page.usageCount--;
                }
            }

            std::cout << "Background Clock Sweep Running..." << std::endl;
        }
    }
};

int main() {
    ClockSweep<int> clockSweep(3);

    clockSweep.putKey(10);
    clockSweep.putKey(20);
    clockSweep.putKey(30);

    clockSweep.getKey(10);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    clockSweep.putKey(40);

    clockSweep.getKey(20);
    clockSweep.getKey(30);
    clockSweep.getKey(40);

    std::this_thread::sleep_for(std::chrono::seconds(5));

    return 0;
}