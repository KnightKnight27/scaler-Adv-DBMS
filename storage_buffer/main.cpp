
#include <iostream>
#include <unordered_map>
#include <deque>
#include <thread>
#include <mutex>
#include <chrono>

template<typename K, typename V>
class ClockSweep {
public:
    struct CacheEntry {
        K key;
        V value;
        bool referenceBit;
        
        CacheEntry(K k, V v) : key(k), value(v), referenceBit(true) {}
    };

    ClockSweep(int maxSize) : maxCacheSize(maxSize), clockHand(0) {
        std::cout << "[ClockSweep] Initialized with max cache size: " << maxCacheSize << std::endl;
    }

    ~ClockSweep() {
        if (bgClockThread.joinable()) {
            bgClockThread.join();
        }
    }

    V getKey(K key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            int index = findIndexByKey(key);
            if (index != -1) {
                cacheList[index].referenceBit = true;
            }
            std::cout << "[GET] Key '" << key << "' found. Value: " << it->second << std::endl;
            return it->second;
        }
        
        std::cout << "[GET] Key '" << key << "' NOT FOUND (cache miss)" << std::endl;
        throw std::runtime_error("Key not found in cache");
    }

    void putKey(K key, V value) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            it->second = value;
            int index = findIndexByKey(key);
            if (index != -1) {
                cacheList[index].referenceBit = true;
            }
            std::cout << "[PUT] Updated key '" << key << "' with value: " << value << std::endl;
            return;
        }

        if (static_cast<int>(cacheMap.size()) >= maxCacheSize) {
            evictByClockSweep();
        }

        cacheList.push_back(CacheEntry(key, value));
        cacheMap[key] = value;
        std::cout << "[PUT] Added key '" << key << "' with value: " << value 
                  << " (cache size: " << cacheMap.size() << "/" << maxCacheSize << ")" << std::endl;
    }

    void printCacheState() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        std::cout << "\n[CACHE STATE] Size: " << cacheMap.size() << "/" << maxCacheSize 
                  << ", Clock hand at: " << clockHand << std::endl;
        
        for (size_t i = 0; i < cacheList.size(); ++i) {
            std::string marker = (i == static_cast<size_t>(clockHand)) ? " <-- CLOCK HAND" : "";
            std::cout << "  [" << i << "] Key: " << cacheList[i].key 
                      << ", Value: " << cacheList[i].value 
                      << ", RefBit: " << cacheList[i].referenceBit << marker << std::endl;
        }
        std::cout << std::endl;
    }

private:
    int maxCacheSize;
    int clockHand;
    std::deque<CacheEntry> cacheList;
    std::unordered_map<K, V> cacheMap;
    std::thread bgClockThread;
    mutable std::mutex cacheMutex;

    void evictByClockSweep() {
        if (cacheList.empty()) {
            return;
        }

        std::cout << "[EVICT] Cache is full. Running Clock Sweep algorithm..." << std::endl;
        
        int attempts = 0;
        int maxAttempts = static_cast<int>(cacheList.size());
        
        while (attempts < maxAttempts) {
            CacheEntry& entry = cacheList[clockHand];
            
            std::cout << "[SWEEP] Checking position " << clockHand << " (key: " << entry.key 
                      << ", refBit: " << entry.referenceBit << ")" << std::endl;
            
            if (entry.referenceBit == 1) {
                entry.referenceBit = false;
                std::cout << "[SWEEP] Reference bit found, clearing and moving to next page" << std::endl;
            } else {
                std::cout << "[SWEEP] Evicting key '" << entry.key << "' (refBit = 0)" << std::endl;
                K victimKey = entry.key;
                cacheMap.erase(victimKey);
                cacheList.erase(cacheList.begin() + clockHand);
                
                if (static_cast<int>(cacheList.size()) > 0) {
                    clockHand = clockHand % static_cast<int>(cacheList.size());
                } else {
                    clockHand = 0;
                }
                
                std::cout << "[EVICT] Successfully evicted key '" << victimKey << "'" << std::endl;
                return;
            }
            
            clockHand = (clockHand + 1) % static_cast<int>(cacheList.size());
            attempts++;
        }
        
        std::cout << "[SWEEP] All pages had reference bit set. Evicting first page as fallback." << std::endl;
        K victimKey = cacheList[0].key;
        cacheMap.erase(victimKey);
        cacheList.pop_front();
        clockHand = 0;
    }

    int findIndexByKey(K key) {
        for (size_t i = 0; i < cacheList.size(); ++i) {
            if (cacheList[i].key == key) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

int main() {
    return 0;
}
