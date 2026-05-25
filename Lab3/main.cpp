#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <chrono>

template<typename T>
class SimpleClockCache {
private:
    // A simple struct, just like in standard introductory classes
    struct CacheItem {
        T value;
        bool isReferenced;
        bool isValid;
        
        // Basic constructor
        CacheItem() {
            value = T();
            isReferenced = false;
            isValid = false;
        }
    };

    int capacity;
    int clockPointer;
    bool keepThreadRunning; // Using a plain bool instead of std::atomic
    
    std::vector<CacheItem> memoryArray;
    std::unordered_map<T, int> locationMap;
    std::thread backgroundThread;

    // Helper function to avoid repeating code
    void addNewItem(int index, T val) {
        memoryArray[index].value = val;
        memoryArray[index].isReferenced = true;
        memoryArray[index].isValid = true;
        
        locationMap[val] = index;
        
        std::cout << "Added item: " << val << "\n";
        
        clockPointer = (clockPointer + 1) % capacity;
    }

    // The thread function
    void sleepLoop() {
        while (keepThreadRunning) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

public:
    SimpleClockCache(int size) {
        capacity = size;
        clockPointer = 0;
        keepThreadRunning = true;
        
        memoryArray.resize(capacity);
        
        // Start the thread
        backgroundThread = std::thread(&SimpleClockCache::sleepLoop, this);
    }

    ~SimpleClockCache() {
        keepThreadRunning = false;
        
        if (backgroundThread.joinable()) {
            backgroundThread.join();
        }
    }

    T fetch(T val) {
        // Standard check to see if key exists
        if (locationMap.find(val) == locationMap.end()) {
            std::cout << "Item missing from cache\n";
            return T(); // Returning a default value (e.g., 0 for int)
        }

        int index = locationMap[val];
        memoryArray[index].isReferenced = true;
        
        std::cout << "Read item: " << val << "\n";
        
        return memoryArray[index].value;
    }

    void store(T val) {
        // If the item is already there, just update the reference bit
        if (locationMap.find(val) != locationMap.end()) {
            int index = locationMap[val];
            memoryArray[index].isReferenced = true;
            
            std::cout << "Item is already in cache: " << val << "\n";
            return;
        }

        // Clock sweep algorithm
        while (true) {
            // Found an empty spot
            if (!memoryArray[clockPointer].isValid) {
                addNewItem(clockPointer, val);
                return;
            }

            // Give a second chance and move on
            if (memoryArray[clockPointer].isReferenced) {
                memoryArray[clockPointer].isReferenced = false;
            } 
            // Evict and replace
            else {
                locationMap.erase(memoryArray[clockPointer].value);
                addNewItem(clockPointer, val);
                return;
            }

            // Move the pointer forward
            clockPointer = (clockPointer + 1) % capacity;
        }
    }

    void printCache() {
        std::cout << "\nCurrent Cache:\n";
        
        // Standard for-loop instead of range-based
        for (int i = 0; i < capacity; i++) {
            if (memoryArray[i].isValid) {
                std::cout << memoryArray[i].value 
                          << " (ref: " << memoryArray[i].isReferenced << ")\n";
            }
        }
        std::cout << "\n";
    }
};

int main() {
    SimpleClockCache<int> myCache(3);

    myCache.store(1);
    myCache.store(2);
    myCache.store(3);

    myCache.printCache();

    myCache.fetch(1);
    myCache.store(4);

    myCache.printCache();

    return 0;
}