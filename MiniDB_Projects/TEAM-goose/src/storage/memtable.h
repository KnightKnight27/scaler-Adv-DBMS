#pragma once

#include "common/types.h"
#include <map>
#include <mutex>

namespace minidb {

// memtable — in-memory sorted write buffer
// backed by std::map<key, record> (red-black tree).  all writes land here
// first.  once the size threshold is reached, the memtable is frozen and
// flushed to an sstable.

class MemTable {
public:
    explicit MemTable(size_t max_entries = 256);

    // insert or update a key → record mapping.  returns true on success.
    bool put(const Key& key, const Record& record);

    // look up a key.  returns record and sets `found`.
    Record get(const Key& key, bool& found) const;

    // delete a key (logical delete — inserts tombstone).  returns true.
    bool remove(const Key& key);

    // iteration support: call for_each with a callback(key, record).
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (const auto& [k, rec] : _map) fn(k, rec);
    }

    size_t size()  const { return _map.size(); }
    bool   empty() const { return _map.empty(); }

    // has the memtable exceeded its flush threshold?
    bool should_flush() const { return _map.size() >= _max_entries; }

    // clear all entries (after flushing).
    void clear() { _map.clear(); }

    // iterator helpers
    auto begin() const { return _map.begin(); }
    auto end()   const { return _map.end(); }

private:
    std::map<Key, Record> _map;
    size_t                _max_entries;
};

// implementation

inline MemTable::MemTable(size_t max_entries) : _max_entries(max_entries) {}

inline bool MemTable::put(const Key& key, const Record& record) {
    _map[key] = record;
    return true;
}

inline Record MemTable::get(const Key& key, bool& found) const {
    auto it = _map.find(key);
    if (it != _map.end()) {
        found = true;
        return it->second;
    }
    found = false;
    return {};
}

inline bool MemTable::remove(const Key& key) {
    _map[key] = {}; // empty record = tombstone
    return true;
}

} // namespace minidb
