#include <iostream>
#include <vector>
#include <stdexcept>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

using namespace std;

template<typename T>
class ClockSweep {
private:
    struct Entry {
        T key;
        T value;
        int accessCount;  // Access count - decremented by background thread
    };
    
    vector<Entry> cache;
    size_t capacity;
    size_t hand;  // Clock hand pointer
    mutex cacheLock;
    thread bgThread;
    atomic<bool> running{false};

public:
    ClockSweep(size_t maxSize) : capacity(maxSize), hand(0) {
        running = true;
        bgThread = thread(&ClockSweep::backgroundSweep, this);
    }
    
    ~ClockSweep() {
        running = false;
        if (bgThread.joinable()) {
            bgThread.join();
        }
    }
    
    T get(T key) {
        unique_lock<mutex> lk(cacheLock);
        
        for (auto& entry : cache) {
            if (entry.key == key) {
                entry.accessCount++;  // Increment access count
                return entry.value;
            }
        }
        
        throw runtime_error("Key not found");
    }
    
    void put(T key, T value) {
        unique_lock<mutex> lk(cacheLock);
        
        // Update if key exists
        for (auto& entry : cache) {
            if (entry.key == key) {
                entry.value = value;
                entry.accessCount++;  // Increment access count
                return;
            }
        }
        
        // If cache is full, evict using clock sweep algorithm
        if (cache.size() >= capacity) {
            evictOne();
        }
        
        // Add new entry
        cache.push_back({key, value, 1});
    }
    
    void display() {
        unique_lock<mutex> lk(cacheLock);
        cout << "Cache contents (hand at [pos]): ";
        for (size_t i = 0; i < cache.size(); i++) {
            if (i == hand) cout << "[";
            cout << "(" << cache[i].key << ":" << cache[i].value << "|acc=" << cache[i].accessCount << ")";
            if (i == hand) cout << "]";
            cout << " ";
        }
        cout << endl;
    }

private:
    void evictOne() {
        // Prioritize evicting entries with accessCount <= 0 (targets for eviction)
        for (size_t i = 0; i < cache.size(); i++) {
            if (cache[i].accessCount <= 0) {
                cache.erase(cache.begin() + i);
                if (hand >= cache.size() && cache.size() > 0) {
                    hand = 0;
                }
                return;
            }
        }
        
        // If no entries with accessCount <= 0, find entry with lowest access count
        int minAcc = cache[0].accessCount;
        size_t minIdx = 0;
        
        for (size_t i = 1; i < cache.size(); i++) {
            if (cache[i].accessCount < minAcc) {
                minAcc = cache[i].accessCount;
                minIdx = i;
            }
        }
        
        // Evict the entry with lowest access count
        cache.erase(cache.begin() + minIdx);
        if (hand >= cache.size() && cache.size() > 0) {
            hand = 0;
        }
    }
    
    void backgroundSweep() {
        while (running) {
            this_thread::sleep_for(chrono::milliseconds(500));
            
            unique_lock<mutex> lk(cacheLock);
            
            if (cache.empty()) continue;
            
            // Decrement access count for all entries
            for (auto& entry : cache) {
                if (entry.accessCount > 0) {
                    entry.accessCount--;
                }
            }
            
            // Move hand to next position
            if (!cache.empty()) {
                hand = (hand + 1) % cache.size();
            }
        }
    }
};


int main() {
    ClockSweep<int> cache(3);  // Cache capacity of 3
    
    cout << "=== Clock Sweep Cache Demo (with Reference Decay) ===" << endl << endl;
    
    // Add 3 items (cache will be full)
    cout << "Adding items: 1, 2, 3" << endl;
    cache.put(1, 100);
    cache.put(2, 200);
    cache.put(3, 300);
    cache.display();
    
    // Access item 1 multiple times (increase reference count)
    cout << "\nAccessing key 1 three times" << endl;
    cout << "Value: " << cache.get(1) << endl;
    cout << "Value: " << cache.get(1) << endl;
    cout << "Value: " << cache.get(1) << endl;
    cache.display();
    
    // Add item 4 - should evict item 2 (acc=1, will decay to 0 first)
    cout << "\nAdding item 4 (cache full, will evict lowest acc count)" << endl;
    cache.put(4, 400);
    cache.display();
    
    // Wait for background thread to decay references
    cout << "\nWaiting 1 second for background decay..." << endl;
    this_thread::sleep_for(chrono::seconds(1));
    cache.display();
    
    // Wait more to see further decay
    cout << "\nWaiting another 1 second..." << endl;
    this_thread::sleep_for(chrono::seconds(1));
    cache.display();
    
    // Add item 5 - items should have decayed, will evict one with ref=0
    cout << "\nAdding item 5" << endl;
    cache.put(5, 500);
    cache.display();
    
    // Try to access evicted item
    cout << "\nTrying to access evicted items..." << endl;
    try {
        cache.get(2);
    } catch (const exception& e) {
        cout << "Item 2: " << e.what() << endl;
    }
    
    return 0;
}
