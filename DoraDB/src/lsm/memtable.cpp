#include "lsm/memtable.h"
#include <cstring>

void MemTable::Put(int key, const char* data, int size) {
    MemEntry entry;
    entry.data.assign(data, data + size);
    entry.size = size;
    entry.deleted = false;
    entries_[key] = entry;
}

void MemTable::Delete(int key) {
    MemEntry entry;
    entry.deleted = true;
    entries_[key] = entry;
}

bool MemTable::Get(int key, char* out_data, int* out_size) const {
    auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    if (it->second.deleted) return false;
    memcpy(out_data, it->second.data.data(), it->second.size);
    *out_size = it->second.size;
    return true;
}

bool MemTable::Contains(int key) const {
    return entries_.count(key) > 0;
}

bool MemTable::IsDeleted(int key) const {
    auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    return it->second.deleted;
}

void MemTable::ForEach(std::function<void(int, const MemEntry&)> fn) const {
    for (auto& [key, entry] : entries_) {
        fn(key, entry);
    }
}

void MemTable::Clear() {
    entries_.clear();
}
