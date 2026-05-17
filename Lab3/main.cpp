#include <iostream>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
template <typename T> class ClockSweep {
public:
  ClockSweep(int maxNumber = 16384) : maxCacheSize(maxNumber) {
    buffer.resize(maxCacheSize);
  }
  ~ClockSweep() = default;
  std::optional<T> getKey(T key) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = index.find(key);
    if (it != index.end()) {
      size_t pos = it->second;
      buffer[pos].inUse = true;
      if (buffer[pos].usageCount < MAX_USAGE_COUNT) {
        buffer[pos].usageCount++;
      }
      return buffer[pos].key;
    }
    return std::nullopt;
  }
  void putKey(T key) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = index.find(key);
    if (it != index.end()) {
      size_t pos = it->second;
      buffer[pos].inUse = true;
      if (buffer[pos].usageCount < MAX_USAGE_COUNT) {
        buffer[pos].usageCount++;
      }
      return;
    }
    size_t pos = findFreeSlot();
    if (pos == maxCacheSize) {
      pos = evict();
      if (pos == maxCacheSize) {
        std::cerr << "Buffer pool is full and all pages are pinned."
                  << std::endl;
        return;
      }
    }
    buffer[pos].key = key;
    buffer[pos].usageCount = 1;
    buffer[pos].inUse = true;
    buffer[pos].occupied = true;
    index[key] = pos;
  }
  void releaseKey(T key) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = index.find(key);
    if (it != index.end()) {
      buffer[it->second].inUse = false;
    }
  }
  void printState() {
    std::lock_guard<std::mutex> lock(mtx);
    std::cout << "Buffer pool state" << std::endl;
    std::cout << "Clock Hand Position: " << clockHand << std::endl;
    for (size_t i = 0; i < maxCacheSize; ++i) {
      if (buffer[i].occupied) {
        std::cout << "Slot " << i << ": key=" << buffer[i].key
                  << " usageCount=" << static_cast<int>(buffer[i].usageCount)
                  << " inUse=" << (buffer[i].inUse ? "true" : "false")
                  << std::endl;
      }
    }
  }
private:
  static constexpr uint8_t MAX_USAGE_COUNT = 5;
  struct Frame {
    T key{};
    uint8_t usageCount = 0;
    bool inUse = false;
    bool occupied = false;
  };
  size_t findFreeSlot() {
    for (size_t i = 0; i < maxCacheSize; ++i) {
      if (!buffer[i].occupied) {
        return i;
      }
    }
    return maxCacheSize;
  }
  size_t evict() {
    size_t startHand = clockHand;
    size_t passes = 0;
    size_t maxPasses = MAX_USAGE_COUNT + 1;
    while (passes < maxPasses) {
      size_t pos = clockHand;
      clockHand = (clockHand + 1) % maxCacheSize;
      if (!buffer[pos].occupied) {
        return pos;
      }
      if (buffer[pos].inUse) {
        if (clockHand == startHand)
          passes++;
        continue;
      }
      if (buffer[pos].usageCount == 0) {
        T evictedKey = buffer[pos].key;
        buffer[pos].occupied = false;
        buffer[pos].inUse = false;
        buffer[pos].usageCount = 0;
        index.erase(evictedKey);
        return pos;
      }
      buffer[pos].usageCount--;
      if (clockHand == startHand) {
        passes++;
      }
    }
    return maxCacheSize;
  }
  unsigned int maxCacheSize;
  std::vector<Frame> buffer;
  std::unordered_map<T, size_t> index;
  size_t clockHand{0};
  std::mutex mtx;
};
int main() {
  ClockSweep<int> clockSweep(5);
  clockSweep.putKey(10);
  clockSweep.putKey(20);
  clockSweep.putKey(30);
  clockSweep.releaseKey(10);
  clockSweep.releaseKey(20);
  clockSweep.releaseKey(30);
  clockSweep.printState();
  clockSweep.putKey(40);
  clockSweep.putKey(50);
  clockSweep.releaseKey(40);
  clockSweep.releaseKey(50);
  clockSweep.putKey(60);
  std::cout << "\nAfter evictions:" << std::endl;
  clockSweep.printState();
  return 0;
}
