
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>

template <typename T>
class ClockSweep {
public:
  explicit ClockSweep(size_t capacity) :frames_(RoundUpToPowerOfTwo(capacity)), hand_(0), next_free_(0), hand_mask_(frames_.empty() ? 0 : frames_.size() - 1) {}

  bool Get(const T &key) {
    const auto it = page_table_.find(key);
    if (it == page_table_.end()) return false;

    Frame &frame = frames_[it->second];
    if (frame.use_cnt < kMaxUseCnt) {
      ++frame.use_cnt;
    }
    return true;
  }

  void Put(const T &key) {
    if (frames_.empty()) return;

    const auto it = page_table_.find(key);
    if (it != page_table_.end()) {
      Frame &frame = frames_[it->second];
      if (frame.use_cnt < kMaxUseCnt) {
        ++frame.use_cnt;
      }
      return;
    }

    if (next_free_ < frames_.size()) {
      LoadIntoFrame(next_free_++, key);
      return;
    }

    const size_t n = frames_.size();
    const size_t max_scans = (kMaxUseCnt + 1) * n;
    for (size_t scanned = 0; scanned < max_scans; ++scanned) {
      Frame &frame = frames_[hand_];
      if (frame.use_cnt > 0) {
        --frame.use_cnt;
        AdvanceHand();
        continue;
      }

      EvictFrame(hand_);
      LoadIntoFrame(hand_, key);
      AdvanceHand();
      return;
    }
  }

  void DebugPrint() const {
    for (const Frame &frame : frames_) {
      if (frame.valid) {
        std::cout << '[' << frame.key << " u=" << static_cast<unsigned>(frame.use_cnt) << "] ";
      } else {
        std::cout << "[free] ";
      }
    }
    std::cout << " hand=" << hand_ << "\n";
  }

private:
  static size_t RoundUpToPowerOfTwo(size_t value) {
    if (value == 0) return 0;

    --value;
    for (size_t shift = 1; shift < sizeof(size_t) * 8; shift <<= 1) {
      value |= (value >> shift);
    }
    return value + 1;
  }

  struct Frame {
    T key{};
    uint8_t use_cnt{0};
    bool valid{false};
  };

  static constexpr uint8_t kMaxUseCnt = 5;

  void AdvanceHand() {
    hand_ = (hand_ + 1) & hand_mask_; 
  }

  void EvictFrame(size_t idx) {
    Frame &frame = frames_[idx];
    if (!frame.valid) return;

    page_table_.erase(frame.key);
    frame.valid = false;
    frame.use_cnt = 0;
  }

  void LoadIntoFrame(size_t idx, const T &key) {
    Frame &frame = frames_[idx];
    frame.key = key;
    frame.use_cnt = 1;
    frame.valid = true;
    page_table_[key] = idx;
  }

private:
  std::vector<Frame> frames_;
  std::unordered_map<T, size_t> page_table_;
  size_t hand_;
  size_t next_free_;
  size_t hand_mask_;
};

int main() {
  ClockSweep<int> clockSweep(3);

  clockSweep.Put(10);
  clockSweep.Put(20);
  clockSweep.Put(30);
  clockSweep.Get(10);
  clockSweep.Get(10);
  clockSweep.Get(20);
  std::cout << "Before eviction: ";
  clockSweep.DebugPrint();

  clockSweep.Put(4);
  std::cout << "After Put(4):   ";
  clockSweep.DebugPrint();

  return 0;
}