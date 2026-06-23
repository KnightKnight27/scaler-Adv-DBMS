// MemTable: the in-memory, write-optimized front of the LSM-tree.
//
// All writes (puts and deletes) land here first, in a sorted std::map so the
// table can be flushed to a sorted SSTable in one linear pass. A delete is a
// "tombstone" entry, so it can shadow older values living in SSTables until
// compaction physically removes them. The MemTable is flushed once it exceeds
// a byte threshold.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace minidb {

struct MemEntry {
    std::string value;
    bool tombstone = false;
};

class MemTable {
public:
    void put(const std::string& key, const std::string& value) {
        size_ += entry_cost(key, value, map_.count(key) ? &map_[key] : nullptr);
        map_[key] = MemEntry{value, false};
    }
    void del(const std::string& key) {
        size_ += entry_cost(key, "", map_.count(key) ? &map_[key] : nullptr);
        map_[key] = MemEntry{"", true};
    }

    // Returns the entry if this key is present here (value or tombstone).
    std::optional<MemEntry> get(const std::string& key) const {
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;
        return it->second;
    }

    const std::map<std::string, MemEntry>& entries() const { return map_; }
    size_t size_bytes() const { return size_; }
    size_t count() const { return map_.size(); }
    bool empty() const { return map_.empty(); }
    void clear() { map_.clear(); size_ = 0; }

private:
    static size_t entry_cost(const std::string& k, const std::string& v, const MemEntry* old) {
        size_t add = k.size() + v.size() + 16;
        if (old) return add;  // overwrite: approximate, don't double count much
        return add;
    }

    std::map<std::string, MemEntry> map_;
    size_t size_ = 0;
};

}  // namespace minidb
