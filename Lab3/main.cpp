#include <iostream>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>

template<typename KeyType>
class ClockSweepManager {
private:
    struct CacheBlock {
        KeyType data;
        int refCount{0};
        bool isOccupied{false};
        bool isLocked{false}; 
    };

    size_t maxCapacity;
    std::vector<CacheBlock> pool;
    std::unordered_map<KeyType, size_t> pageTable;
    size_t sweepPointer{0}; 

    std::mutex cacheMutex;
    std::atomic<bool> isRunning{true};
    std::thread sweeperThread;

    void backgroundSweeper() {
        while (isRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            std::lock_guard<std::mutex> lock(cacheMutex);
            
            for (auto& block : pool) {
                if (block.isOccupied && !block.isLocked && block.refCount > 0) {
                    block.refCount--;
                }
            }
        }
    }

    size_t findVictim() {
        while (true) {
            if (sweepPointer >= maxCapacity) {
                sweepPointer = 0;
            }

            if (!pool[sweepPointer].isOccupied) {
                return sweepPointer;
            }

            if (!pool[sweepPointer].isLocked) {
                if (pool[sweepPointer].refCount == 0) {
                    return sweepPointer; 
                } else {
                    pool[sweepPointer].refCount--; 
                }
            }
            sweepPointer++;
        }
    }

public:
    explicit ClockSweepManager(size_t capacity) : maxCapacity(capacity) {
        pool.resize(maxCapacity);
        sweeperThread = std::thread(&ClockSweepManager::backgroundSweeper, this);
    }

    ~ClockSweepManager() {
        isRunning = false;
        if (sweeperThread.joinable()) {
            sweeperThread.join();
        }
    }

    bool retrieve(KeyType key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = pageTable.find(key);
        
        if (it != pageTable.end()) {
            size_t idx = it->second;
            if (pool[idx].refCount < 3) {
                pool[idx].refCount++;
            }
            std::cout << "Hit: " << key << "\n";
            return true;
        }
        
        std::cout << "Miss: " << key << "\n";
        return false;
    }

    void add(KeyType key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        if (pageTable.count(key)) {
            size_t idx = pageTable[key];
            if (pool[idx].refCount < 3) pool[idx].refCount++;
            std::cout << "Exists: " << key << "\n";
            return;
        }

        size_t victimIdx = findVictim();

        if (pool[victimIdx].isOccupied) {
            std::cout << "Evict: " << pool[victimIdx].data << " -> " << key << "\n";
            pageTable.erase(pool[victimIdx].data);
        } else {
            std::cout << "Insert: " << key << "\n";
        }

        pool[victimIdx].data = key;
        pool[victimIdx].refCount = 1;
        pool[victimIdx].isOccupied = true;
        pool[victimIdx].isLocked = false;
        pageTable[key] = victimIdx;

        sweepPointer = (victimIdx + 1) % maxCapacity;
    }

    void displayState() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        std::cout << "Cache: ";
        for (size_t i = 0; i < maxCapacity; ++i) {
            if (pool[i].isOccupied) {
                std::cout << "[" << pool[i].data << "|" << pool[i].refCount << "]";
                if (sweepPointer == i) std::cout << "* ";
                else std::cout << " ";
            } else {
                std::cout << "[ ] ";
            }
        }
        std::cout << "\n";
    }
};

int main() {
    ClockSweepManager<int> manager(3);

    manager.add(10);
    manager.add(20);
    manager.add(30);

    manager.displayState();

    std::this_thread::sleep_for(std::chrono::seconds(2));

    manager.retrieve(10); 
    
    manager.displayState();

    manager.add(40); 

    manager.displayState();

    return 0;
}