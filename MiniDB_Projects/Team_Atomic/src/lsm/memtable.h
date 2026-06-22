#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace minidb {

// A value in the LSM. A delete is a tombstone (not a removal) so it can shadow
// older versions until compaction drops it.
struct LsmValue {
  std::string data;
  bool tombstone = false;
};

// The LSM's in-memory write buffer. A std::map keeps keys sorted, so a flush
// is one ordered pass to an SSTable.
class MemTable {
 public:
  void Put(int64_t key, const std::string& value) {
    Account(key, value.size(), false);
    map_[key] = {value, false};
  }
  void Delete(int64_t key) {
    Account(key, 0, true);
    map_[key] = {"", true};
  }
  bool Get(int64_t key, LsmValue* out) const {
    auto it = map_.find(key);
    if (it == map_.end()) return false;
    *out = it->second;
    return true;
  }

  size_t Bytes() const { return bytes_; }
  size_t Count() const { return map_.size(); }
  bool Empty() const { return map_.empty(); }
  const std::map<int64_t, LsmValue>& Entries() const { return map_; }
  void Clear() { map_.clear(); bytes_ = 0; }

 private:
  void Account(int64_t key, size_t vsize, bool) {
    auto it = map_.find(key);
    if (it != map_.end()) bytes_ -= it->second.data.size() + sizeof(int64_t);
    bytes_ += vsize + sizeof(int64_t);
  }
  std::map<int64_t, LsmValue> map_;
  size_t bytes_ = 0;
};

}  // namespace minidb
