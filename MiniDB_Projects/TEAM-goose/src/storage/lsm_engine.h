#pragma once

#include "memtable.h"
#include "sstable.h"
#include "compaction.h"
#include "buffer_pool.h"
#include <mutex>
#include <atomic>

namespace minidb {

// lsmengine — lsm-tree based storage engine (track c)
//
// write path:  put() → active memtable → (when full) flush to sstable
// read path:   get() → check active memtable → frozen memtables → sstables
// compaction:  background merge of sstables when count > threshold
//
// this replaces the heap-file storage engine required by the core spec.
// all reads check newest data first (active memtable → frozen → sstables)
// to ensure correctness.

class LSMEngine {
public:
    LSMEngine(const std::string& data_dir, size_t memtable_max = 256,
              size_t sstable_merge_threshold = 4);

    // --- core crud ----------------------------------------------------------
    // insert or update a row.  the table_id and key uniquely identify a row.
    void put(TableID table_id, const Key& key, const Record& record);

    // read a row.  returns empty record if not found (check `found` flag).
    Record get(TableID table_id, const Key& key, bool& found);

    // delete a row (logical delete via tombstone).
    void remove(TableID table_id, const Key& key);

    // direct delete using an already-composite key (from scan callbacks).
    void remove_direct(const Key& composite_key);

    // --- scan operations ----------------------------------------------------
    // range scan: call fn(key, record) for every row in [start, end] for
    // the given table.
    template <typename Fn>
    void scan(TableID table_id, const Key& start, const Key& end, Fn&& fn);

    // full table scan.
    template <typename Fn>
    void full_scan(TableID table_id, Fn&& fn);

    // --- management ---------------------------------------------------------
    void flush();       // force flush active memtable
    void compact();     // force compaction of sstables
    void close();       // flush and clean up

    size_t memtable_size() const { return _active_memtable.size(); }
    size_t sstable_count() const { return _sstables.size(); }

    const std::string& data_dir() const { return _data_dir; }

private:
    std::string key_for_table(TableID table_id, const Key& key) const;
    void        flush_memtable(MemTable& mt, int level);
    void        try_compact();

    std::string                _data_dir;
    size_t                     _memtable_max;
    size_t                     _sstable_merge_threshold;

    MemTable                   _active_memtable;
    std::vector<MemTable>      _frozen_memtables;
    std::vector<SSTable>       _sstables;

    std::atomic<int>           _sstable_counter{0};
    mutable std::mutex         _mutex;
};

// implementation

inline LSMEngine::LSMEngine(const std::string& data_dir, size_t memtable_max,
                             size_t sstable_merge_threshold)
    : _data_dir(data_dir)
    , _memtable_max(memtable_max)
    , _sstable_merge_threshold(sstable_merge_threshold)
    , _active_memtable(memtable_max) {
    std::filesystem::create_directories(data_dir);
}

inline std::string LSMEngine::key_for_table(TableID table_id, const Key& key) const {
    // composite key: table_id (4 bytes) + key bytes → prefix encodes table
    // this allows all tables to share the same lsm-tree safely.
    std::ostringstream oss(std::ios::binary);
    write_u32(oss, table_id);
    write_value(oss, key);
    return oss.str();
}

inline void LSMEngine::put(TableID table_id, const Key& key, const Record& record) {
    std::lock_guard<std::mutex> lock(_mutex);
    Key composite_key;
    composite_key.str_val = key_for_table(table_id, key);
    composite_key.type = ValueType::STRING;
    _active_memtable.put(composite_key, record);
    if (_active_memtable.should_flush()) flush();
}

inline Record LSMEngine::get(TableID table_id, const Key& key, bool& found) {
    std::lock_guard<std::mutex> lock(_mutex);
    Key composite_key;
    composite_key.str_val = key_for_table(table_id, key);
    composite_key.type = ValueType::STRING;

    // 1. check active memtable
    Record rec = _active_memtable.get(composite_key, found);
    if (found) {
        return rec.empty() ? Record{} : rec; // empty = tombstone
    }

    // 2. check frozen memtables (newest first)
    for (auto it = _frozen_memtables.rbegin(); it != _frozen_memtables.rend(); ++it) {
        rec = it->get(composite_key, found);
        if (found) return rec.empty() ? Record{} : rec;
    }

    // 3. check sstables (newest first — higher counter = newer)
    for (auto it = _sstables.rbegin(); it != _sstables.rend(); ++it) {
        rec = it->get(composite_key, found);
        if (found) return rec.empty() ? Record{} : rec;
    }

    found = false;
    return {};
}

inline void LSMEngine::remove(TableID table_id, const Key& key) {
    std::lock_guard<std::mutex> lock(_mutex);
    Key composite_key;
    composite_key.str_val = key_for_table(table_id, key);
    composite_key.type = ValueType::STRING;
    _active_memtable.remove(composite_key);
    if (_active_memtable.should_flush()) flush();
}

inline void LSMEngine::remove_direct(const Key& composite_key) {
    std::lock_guard<std::mutex> lock(_mutex);
    _active_memtable.remove(composite_key);
    if (_active_memtable.should_flush()) flush();
}

template <typename Fn>
void LSMEngine::scan(TableID /*table_id*/, const Key& start, const Key& end, Fn&& fn) {
    std::lock_guard<std::mutex> lock(_mutex);

    std::map<Key, Record> results;

    auto add_if_in_range = [&](const Key& k, const Record& rec) {
        if (k >= start && k <= end) {
            if (rec.empty()) results.erase(k);
            else             results[k] = rec;
        }
    };

    for (auto& sst : _sstables) {
        sst.scan(start, end, add_if_in_range);
    }
    for (auto& mt : _frozen_memtables) {
        mt.for_each(add_if_in_range);
    }
    _active_memtable.for_each(add_if_in_range);

    for (auto& [k, rec] : results) fn(k, rec);
}

template <typename Fn>
void LSMEngine::full_scan(TableID table_id, Fn&& fn) {
    // use extreme bounds
    Key low, high;
    low.str_val  = key_for_table(table_id, Value(INT32_MIN));
    low.type = ValueType::STRING;
    high.str_val = key_for_table(table_id, Value(INT32_MAX));
    high.type = ValueType::STRING;
    scan(table_id, low, high, std::forward<Fn>(fn));
}

inline void LSMEngine::flush() {
    if (_active_memtable.empty()) return;

    MemTable frozen(std::move(_active_memtable));
    _active_memtable = MemTable(_memtable_max);

    _frozen_memtables.push_back(std::move(frozen));
    flush_memtable(_frozen_memtables.back(), 0);
    try_compact();
}

inline void LSMEngine::flush_memtable(MemTable& mt, int /*level*/) {
    int id = ++_sstable_counter;
    std::string path = _data_dir + "/sstable_" + std::to_string(id) + ".sst";

    SSTable sst;
    sst.build(path, mt.begin(), mt.end());
    _sstables.push_back(std::move(sst));
}

inline void LSMEngine::compact() {
    std::lock_guard<std::mutex> lock(_mutex);
    try_compact();
}

inline void LSMEngine::try_compact() {
    if (_sstables.size() < _sstable_merge_threshold) return;

    // merge all current sstables into a new one
    std::vector<SSTable*> inputs;
    for (auto& sst : _sstables) inputs.push_back(&sst);

    int id = ++_sstable_counter;
    std::string output_path = _data_dir + "/sstable_" + std::to_string(id) + ".sst";

    if (Compaction::compact(inputs, output_path)) {
        _sstables.clear();
        SSTable merged;
        if (merged.open(output_path)) {
            _sstables.push_back(std::move(merged));
        }
    }
}

inline void LSMEngine::close() {
    std::lock_guard<std::mutex> lock(_mutex);
    flush();
    // clean up frozen memtables (already flushed)
    _frozen_memtables.clear();
}

} // namespace minidb
