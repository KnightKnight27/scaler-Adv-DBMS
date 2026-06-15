#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

template <typename T> struct CacheEntry {
  T key;
  bool use;
  typename std::vector<T>::iterator it;
};

template <typename T> class ClockSweep {
private:
  std::unordered_map<T, CacheEntry<T>> cache;
  std::vector<T> order;
  size_t maxSize;
  size_t clockHand;
  std::thread clockThread;
  std::atomic<bool> running;
  std::mutex mtx;

  void clockHandThread() {
    while (running.load()) {
      {
        std::lock_guard<std::mutex> lock(mtx);
        if (!cache.empty()) {
          advanceClockHand();
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  void advanceClockHand() {
    if (cache.empty())
      return;

    size_t attempts = 0;
    size_t maxAttempts = cache.size() * 2;

    while (attempts < maxAttempts) {
      if (order.empty())
        break;

      size_t idx = clockHand % order.size();
      auto keyIt = cache.find(order[idx]);

      if (keyIt == cache.end()) {
        clockHand++;
        attempts++;
        continue;
      }

      if (!keyIt->second.use) {
        return;
      }

      keyIt->second.use = false;
      clockHand++;
      attempts++;
    }
  }

  void evictOne() {
    if (cache.empty() || order.empty())
      return;

    size_t attempts = 0;

    while (attempts < cache.size()) {
      size_t idx = clockHand % order.size();
      T key = order[idx];

      auto it = cache.find(key);
      if (it == cache.end()) {
        clockHand++;
        attempts++;
        continue;
      }

      if (!it->second.use) {
        cache.erase(it);
        if (idx < order.size()) {
          order.erase(order.begin() + static_cast<long>(idx));
        }
        return;
      }

      it->second.use = false;
      clockHand++;
      attempts++;
    }

    auto lastIt = cache.find(order.back());
    if (lastIt != cache.end()) {
      cache.erase(lastIt);
    }
    order.pop_back();
  }

public:
  explicit ClockSweep(size_t maxCacheSize)
      : maxSize(maxCacheSize), clockHand(0), running(true) {
    order.reserve(maxSize);
    clockThread = std::thread(&ClockSweep::clockHandThread, this);
  }

  ~ClockSweep() {
    running.store(false);
    if (clockThread.joinable()) {
      clockThread.join();
    }
  }

  std::optional<T> get(T key) {
    std::lock_guard<std::mutex> lock(mtx);

    auto it = cache.find(key);
    if (it != cache.end()) {
      it->second.use = true;
      return key;
    }
    return std::nullopt;
  }

  void put(T key) {
    std::lock_guard<std::mutex> lock(mtx);

    auto it = cache.find(key);
    if (it != cache.end()) {
      it->second.use = true;
      for (size_t i = 0; i < order.size(); i++) {
        if (order[i] == key) {
          order.erase(order.begin() + static_cast<long>(i));
          break;
        }
      }
      order.insert(order.begin(), key);
      it->second.it = order.begin();
      return;
    }

    while (cache.size() >= maxSize) {
      evictOne();
    }

    order.insert(order.begin(), key);
    CacheEntry<T> entry;
    entry.key = key;
    entry.use = true;
    entry.it = order.begin();
    cache[key] = entry;
  }

  size_t size() const { return cache.size(); }

  bool contains(T key) const { return cache.find(key) != cache.end(); }

  void clear() {
    std::lock_guard<std::mutex> lock(mtx);
    cache.clear();
    order.clear();
    clockHand = 0;
  }

  void printState() {
    std::lock_guard<std::mutex> lock(mtx);
    std::cout << "Cache size: " << cache.size() << "/" << maxSize << "\n";
    std::cout << "Clock hand: " << clockHand << "\n";
    std::cout << "Entries: ";
    for (const auto &key : order) {
      auto cit = cache.find(key);
      if (cit != cache.end()) {
        std::cout << key << "(use=" << (cit->second.use ? "1" : "0") << ") ";
      }
    }
    std::cout << "\n";
  }
};

int main() {
  std::cout << "=== Clock Sweep Cache Demo ===\n\n";

  ClockSweep<int> cache(4);

  std::cout << "Test 1: Basic insertions\n";
  cache.put(1);
  cache.put(2);
  cache.put(3);
  cache.put(4);
  cache.printState();

  std::cout << "\nTest 2: Access existing (sets use bit)\n";
  cache.get(1);
  cache.get(2);
  cache.printState();

  std::cout << "\nTest 3: Eviction when full\n";
  cache.put(5);
  cache.printState();

  std::cout << "\nTest 4: Access and update\n";
  cache.get(5);
  cache.put(6);
  cache.printState();

  std::cout << "\nTest 5: Fill again\n";
  cache.put(7);
  cache.put(8);
  cache.printState();

  std::cout << "\nTest 6: Stress test - more inserts than capacity\n";
  ClockSweep<int> stressCache(3);
  for (int i = 0; i < 10; i++) {
    stressCache.put(i * 10);
    std::cout << "After put(" << i * 10 << "): ";
    stressCache.printState();
  }

  std::cout << "\n=== Demo Complete ===\n";
  return 0;
}