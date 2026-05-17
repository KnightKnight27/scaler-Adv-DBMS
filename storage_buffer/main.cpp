
#include <iostream>
#include <vector>
#include <unordered_map>
#include <cstdint>

template<typename T>
class ClockSweep {
public:
  explicit ClockSweep(std::size_t maxNumber)
    : maxCacheSize(maxNumber)
  {
    slots.reserve(maxCacheSize);
  }

  void putKey(const T &key) {
    // simple insert (similar behaviour to code.cpp demo)
    if (keyToSlot.find(key) != keyToSlot.end()) {
      slots[keyToSlot[key]].usageBit = true;
      return;
    }

    if (slots.size() < maxCacheSize) {
      slots.push_back({key, true});
      keyToSlot[key] = static_cast<int>(slots.size() - 1);
      return;
    }

    int victim = findVictim();
    keyToSlot.erase(slots[victim].key);
    slots[victim].key = key;
    slots[victim].usageBit = true;
    keyToSlot[key] = victim;
    clockHand = (victim + 1) % static_cast<int>(maxCacheSize);
  }

  bool getKey(const T &key) {
    auto it = keyToSlot.find(key);
    if (it == keyToSlot.end()) return false;
    slots[it->second].usageBit = true;
    return true;
  }

  void debugPrint() const {
    std::cout << "Buffer state:\n";
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
      std::cout << " frame " << i << " page=" << slots[i].key
            << " bit=" << slots[i].usageBit
            << (i == clockHand ? " <- hand" : "")
            << "\n";
    }
  }

private:
  struct Slot { T key; bool usageBit = false; };

  int findVictim() {
    while (true) {
      if (slots[clockHand].usageBit) {
        slots[clockHand].usageBit = false;
        clockHand = (clockHand + 1) % static_cast<int>(maxCacheSize);
      } else {
        return clockHand;
      }
    }
  }

  std::size_t maxCacheSize{0};
  std::vector<Slot> slots;
  std::unordered_map<T, int> keyToSlot;
  int clockHand = 0;
};

int main() {
  ClockSweep<int> clock(3);
  std::vector<int> accesses = {1,2,3,2,4,1,5};
  for (int k : accesses) {
    bool hit = clock.getKey(k);
    if (!hit) {
      std::cout << "miss " << k << " -> inserting\n";
      clock.putKey(k);
    } else {
      std::cout << "hit " << k << "\n";
    }
  }
  clock.debugPrint();
  return 0;
}