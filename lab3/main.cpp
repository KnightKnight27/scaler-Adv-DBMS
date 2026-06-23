
#include <iostream>
#include <vector>
#include <optional>
#include <cstdint>
#include <mutex>

template<typename T>
class ClockSweep {
public:
  explicit ClockSweep(size_t maxNumber) : maxCacheSize(maxNumber), hand(0) {
    frames.resize(maxCacheSize);
  }

  // Access: return true if key present; increment usage_count (cap at maxUsage)
  bool getKey(const T &key) {
    std::lock_guard<std::mutex> lk(mtx);
    for (auto &f : frames) {
      if (f.occupied && f.key.has_value() && f.key.value() == key) {
        if (f.usage_count < maxUsage) ++f.usage_count;
        return true;
      }
    }
    return false;
  }

  // Insert or update key. If needed, evict using the clock sweep algorithm.
  void putKey(const T &key) {
    std::lock_guard<std::mutex> lk(mtx);

      // if already present, bump usage
    for (auto &f : frames) {
      if (f.occupied && f.key.has_value() && f.key.value() == key) {
        if (f.usage_count < maxUsage) ++f.usage_count;
        return;
      }
    }

    // try to find a free frame
    for (auto &f : frames) {
      if (!f.occupied) {
        f.key = key;
        f.occupied = true;
        f.usage_count = 1;
        return;
      }
    }

    // No free frame: run clock sweep to find victim
    size_t attempts = 0;
    while (true) {
      Frame &f = frames[hand];
      if (f.usage_count > 0) {
        --f.usage_count; // give a second chance
      } else {
        if (!f.pinned) {
          // evict and replace
          f.key = key;
          f.occupied = true;
          f.usage_count = 1;
          hand = (hand + 1) % maxCacheSize;
          return;
        }
      }
      hand = (hand + 1) % maxCacheSize;
      if (++attempts > maxCacheSize * 2) {
        // fallback: force-evict first non-pinned frame
        for (size_t i = 0; i < maxCacheSize; ++i) {
          size_t idx = (hand + i) % maxCacheSize;
          if (!frames[idx].pinned) {
            frames[idx].key = key;
            frames[idx].occupied = true;
            frames[idx].usage_count = 1;
            hand = (idx + 1) % maxCacheSize;
            return;
          }
        }
      }
    }
  }

  // Debug helper
  void debugPrint() const {
    std::lock_guard<std::mutex> lk(mtx);
    std::cout << "Frames:\n";
    for (size_t i = 0; i < frames.size(); ++i) {
      const Frame &f = frames[i];
      std::cout << i << ": ";
      if (!f.occupied) std::cout << "[empty]";
      else std::cout << "key=" << f.key.value() << " u=" << int(f.usage_count) << (f.pinned?" p":"");
      if (i == hand) std::cout << " <-- hand";
      std::cout << "\n";
    }
  }

private:
  struct Frame {
    std::optional<T> key;
    uint8_t usage_count{0};
    bool pinned{false};
    bool occupied{false};
  };

  size_t maxCacheSize{0u};
  std::vector<Frame> frames;
  size_t hand{0};
  mutable std::mutex mtx;
  static constexpr uint8_t maxUsage = 5;
};

int main() {
  ClockSweep<int> clockSweep(3);

  std::cout << "Initial state:\n";
  clockSweep.debugPrint();

  std::cout << "\nInserting 1,2,3\n";
  clockSweep.putKey(1);
  clockSweep.putKey(2);
  clockSweep.putKey(3);
  clockSweep.debugPrint();

  std::cout << "\nAccessing 1 twice, 2 once\n";
  clockSweep.getKey(1);
  clockSweep.getKey(1);
  clockSweep.getKey(2);
  clockSweep.debugPrint();

  std::cout << "\nInserting 4 (should evict a frame with usage_count==0)\n";
  clockSweep.putKey(4);
  clockSweep.debugPrint();

  std::cout << "\nAccess pattern: touch 4 and 3, then insert 5\n";
  clockSweep.getKey(4);
  clockSweep.getKey(3);
  clockSweep.putKey(5);
  clockSweep.debugPrint();

  return 0;
}
