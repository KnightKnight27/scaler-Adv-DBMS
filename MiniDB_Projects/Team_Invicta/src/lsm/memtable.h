#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace minidb {

// One stored version of a key in the LSM tree: either a value or a tombstone
// (a marker recording that the key was deleted).
struct LSMEntry {
  bool        deleted{false};
  std::string value;
};

// The in-memory write buffer of the LSM tree: a sorted map of key -> entry.
// All writes land here first; when it grows past a threshold it is flushed to
// an immutable SSTable. Being a std::map keeps entries sorted for cheap
// flushing and merge.
class MemTable {
 public:
  void Put(int64_t key, const std::string &value) {
    bytes_ += Delta(key, value.size(), false);
    table_[key] = LSMEntry{false, value};
  }
  void Delete(int64_t key) {
    bytes_ += Delta(key, 0, true);
    table_[key] = LSMEntry{true, ""};
  }

  // Look up a key. Returns true if this MemTable has a version of it (which may
  // be a tombstone — check entry->deleted).
  bool Get(int64_t key, LSMEntry *entry) const {
    auto it = table_.find(key);
    if (it == table_.end()) return false;
    *entry = it->second;
    return true;
  }

  bool   empty() const { return table_.empty(); }
  size_t size() const { return table_.size(); }
  size_t bytes() const { return bytes_; }
  void   Clear() { table_.clear(); bytes_ = 0; }

  const std::map<int64_t, LSMEntry> &entries() const { return table_; }

 private:
  // Rough accounting of bytes occupied (16B key+overhead + value).
  size_t Delta(int64_t, size_t vlen, bool) const { return 24 + vlen; }

  std::map<int64_t, LSMEntry> table_;
  size_t                      bytes_{0};
};

}  // namespace minidb
