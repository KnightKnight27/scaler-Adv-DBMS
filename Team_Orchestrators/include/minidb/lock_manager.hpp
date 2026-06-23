#pragma once

#include <cstdint>
#include <unordered_map>

namespace minidb {

class LockManager {
 public:
  enum class Mode { Shared, Exclusive };

  void acquire(uint32_t table, Mode mode);
  void release(uint32_t table);

 private:
  struct LockState {
    size_t shared_count = 0;
    bool exclusive = false;
  };

  std::unordered_map<uint32_t, LockState> locks_;
};

}  // namespace minidb
