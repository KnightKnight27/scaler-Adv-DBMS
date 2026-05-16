#include <iostream>
#include <thread>
#include <unordered_map>
#include <vector>

template <typename T>
class ClockSweep {
 public:
  explicit ClockSweep(int maxNumber)
      : maxCacheSize(static_cast<unsigned int>(maxNumber)),
        entries(maxCacheSize) {}

  T getKey(const T& key) {
    auto it = indexByKey.find(key);
    if (it == indexByKey.end()) {
      return T{};
    }
    entries[it->second].refBit = true;
    return key;
  }

  void putKey(const T& key) {
    auto it = indexByKey.find(key);
    if (it != indexByKey.end()) {
      entries[it->second].refBit = true;
      return;
    }

    if (size < maxCacheSize) {
      entries[size] = Entry{key, true, true};
      indexByKey[key] = size;
      ++size;
      return;
    }

    while (true) {
      Entry& entry = entries[clockHand];
      if (!entry.occupied) {
        entry = Entry{key, true, true};
        indexByKey[key] = clockHand;
        advanceHand();
        return;
      }
      if (!entry.refBit) {
        indexByKey.erase(entry.key);
        entry = Entry{key, true, true};
        indexByKey[key] = clockHand;
        advanceHand();
        return;
      }
      entry.refBit = false;
      advanceHand();
    }
  }

 private:
  struct Entry {
    T key{};
    bool refBit{false};
    bool occupied{false};
  };

  void advanceHand() {
    if (maxCacheSize == 0) {
      return;
    }
    clockHand = (clockHand + 1) % maxCacheSize;
  }

  unsigned int maxCacheSize{0u};
  std::thread bgClockThread;
  std::vector<Entry> entries;
  std::unordered_map<T, unsigned int> indexByKey;
  unsigned int clockHand{0u};
  unsigned int size{0u};
};

int main() {
  ClockSweep<int> clockSweep(3);

  clockSweep.putKey(10);
  clockSweep.putKey(20);
  clockSweep.putKey(30);

  std::cout << "Get 20 -> " << clockSweep.getKey(20) << '\n';

  clockSweep.putKey(40);
  std::cout << "Get 10 -> " << clockSweep.getKey(10) << '\n';
  std::cout << "Get 40 -> " << clockSweep.getKey(40) << '\n';

  return 0;
}
