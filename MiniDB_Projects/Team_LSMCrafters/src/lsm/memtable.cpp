#include "lsm/memtable.h"

namespace minidb {

void MemTable::put(Key key, Bytes value, SeqNo seq) {
  bytes_ += sizeof(Key) + value.size() + sizeof(ValueEntry);
  table_[key] = ValueEntry{RecType::Put, seq, std::move(value)};
}

void MemTable::del(Key key, SeqNo seq) {
  bytes_ += sizeof(Key) + sizeof(ValueEntry);
  table_[key] = ValueEntry{RecType::Tombstone, seq, {}};
}

std::optional<ValueEntry> MemTable::get(Key key) const {
  auto it = table_.find(key);
  if (it == table_.end()) return std::nullopt;
  return it->second;
}

}  // namespace minidb
