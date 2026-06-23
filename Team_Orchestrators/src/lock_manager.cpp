#include "minidb/lock_manager.hpp"
#include <stdexcept>

namespace minidb {

void LockManager::acquire(uint32_t table, Mode mode) {
  LockState& state = locks_[table];
  if (mode == Mode::Shared) {
    if (state.exclusive) throw std::runtime_error("lock conflict: exclusive lock held");
    ++state.shared_count;
  } else {
    if (state.exclusive || state.shared_count > 0) throw std::runtime_error("lock conflict: cannot acquire exclusive lock");
    state.exclusive = true;
  }
}

void LockManager::release(uint32_t table) {
  auto it = locks_.find(table);
  if (it == locks_.end()) return;
  if (it->second.exclusive) {
    it->second.exclusive = false;
  } else if (it->second.shared_count > 0) {
    --it->second.shared_count;
  }
  if (it->second.shared_count == 0 && !it->second.exclusive) locks_.erase(it);
}

}  // namespace minidb
