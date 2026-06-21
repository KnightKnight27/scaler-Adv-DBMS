#include "lsm/memtable.h"

namespace walterdb {

void MemTable::put(std::string_view key, std::string_view value) {
  auto it = table_.find(key);
  if (it == table_.end()) {
    bytes_ += key.size() + value.size() + sizeof(MemEntry);
    table_.emplace(std::string(key), MemEntry{std::string(value), false});
  } else {
    bytes_ += value.size();
    bytes_ -= it->second.value.size();
    it->second.value.assign(value.begin(), value.end());
    it->second.tombstone = false;
  }
}

void MemTable::remove(std::string_view key) {
  auto it = table_.find(key);
  if (it == table_.end()) {
    bytes_ += key.size() + sizeof(MemEntry);
    table_.emplace(std::string(key), MemEntry{std::string(), true});
  } else {
    bytes_ -= it->second.value.size();
    it->second.value.clear();
    it->second.tombstone = true;
  }
}

MemTable::Lookup MemTable::get(std::string_view key, std::string* out) const {
  auto it = table_.find(key);
  if (it == table_.end()) return Lookup::Absent;
  if (it->second.tombstone) return Lookup::Tombstone;
  if (out) *out = it->second.value;
  return Lookup::Found;
}

void MemTable::clear() {
  table_.clear();
  bytes_ = 0;
}

}  // namespace walterdb
