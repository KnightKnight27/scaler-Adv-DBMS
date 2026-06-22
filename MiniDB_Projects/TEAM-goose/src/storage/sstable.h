#pragma once

#include "common/types.h"
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>

namespace minidb {

// sstable — sorted string table (immutable, on disk)
// format:
//   [data block]  — sequence of (key_len, key, record) entries, sorted
//   [index block] — sequence of (key, offset) for binary search
//   [footer]      — index_offset (u32), entry_count (u32), magic (u32)
//
// once written, an sstable is never modified.  new versions are created
// during compaction.

class SSTable {
public:
    SSTable() = default;

    // --- write path ---------------------------------------------------------
    // build an sstable from an iterator of (key, record) pairs.
    // the iterator must yield entries in sorted key order.
    template <typename Iter>
    void build(const std::string& filepath, Iter begin, Iter end);

    // --- read path ----------------------------------------------------------
    // open an existing sstable file for reading.
    bool open(const std::string& filepath);

    // point lookup: binary search through index, then scan data block.
    Record get(const Key& key, bool& found) const;

    // range scan: call `fn(key, record)` for every entry in [start, end].
    template <typename Fn>
    void scan(const Key& start, const Key& end, Fn&& fn) const;

    // iterate all entries.
    template <typename Fn>
    void for_each(Fn&& fn) const;

    size_t entry_count() const { return _entry_count; }
    std::string filepath() const { return _filepath; }

    // delete the file on disk.
    void remove_file();

    // allow compaction to access internals for k-way merge.
    friend class Compaction;

private:
    // internal helper: read entry at a given file offset.
    std::pair<Key, Record> read_entry_at(uint32_t offset) const;

    std::string   _filepath;
    size_t        _entry_count = 0;
    uint32_t      _index_offset = 0;   // where index block starts

    // cached index entries for binary search.
    std::vector<std::pair<Key, uint32_t>> _index;

    // in-memory cache of the full data for simplicity (fits in memory for
    // a college project).  production systems would mmap the file instead.
    std::string   _data_buffer;
};

// write path

template <typename Iter>
void SSTable::build(const std::string& filepath, Iter begin, Iter end) {
    _filepath = filepath;
    _entry_count = 0;
    _index.clear();

    std::ostringstream data_stream(std::ios::binary);

    // write data block entries
    for (auto it = begin; it != end; ++it) {
        uint32_t offset = static_cast<uint32_t>(data_stream.tellp());

        // store (key, offset) for index — one entry every n entries is fine
        // for simplicity we index every entry.
        _index.emplace_back(it->first, offset);

        // serialise: key_len (u32) + key bytes + record bytes
        write_value(data_stream, it->first);
        write_record(data_stream, it->second);
        ++_entry_count;
    }

    // write index block
    uint32_t index_start = static_cast<uint32_t>(data_stream.tellp());
    write_u32(data_stream, static_cast<uint32_t>(_index.size()));
    for (const auto& [key, off] : _index) {
        write_value(data_stream, key);
        write_u32(data_stream, off);
    }

    // write footer
    const uint32_t MAGIC = 0x53535442; // "sstb"
    write_u32(data_stream, index_start);
    write_u32(data_stream, static_cast<uint32_t>(_entry_count));
    write_u32(data_stream, MAGIC);

    // flush to disk
    std::ofstream ofs(filepath, std::ios::binary);
    ofs << data_stream.str();
    ofs.close();

    // cache the data buffer for reads
    _data_buffer = data_stream.str();
    _index_offset = index_start;
}

// read path

inline bool SSTable::open(const std::string& filepath) {
    _filepath = filepath;
    std::ifstream ifs(filepath, std::ios::binary | std::ios::ate);
    if (!ifs.good()) return false;

    size_t file_size = ifs.tellg();
    ifs.seekg(0);

    _data_buffer.resize(file_size);
    ifs.read(&_data_buffer[0], static_cast<std::streamsize>(file_size));
    ifs.close();

    if (file_size < 12) return false;

    // read footer (last 12 bytes)
    uint32_t magic = 0, entry_cnt = 0;
    std::memcpy(&_index_offset, &_data_buffer[file_size - 12], 4);
    std::memcpy(&entry_cnt,     &_data_buffer[file_size - 8],  4);
    std::memcpy(&magic,         &_data_buffer[file_size - 4],  4);

    const uint32_t MAGIC = 0x53535442;
    if (magic != MAGIC) return false;

    _entry_count = entry_cnt;

    // read index block
    std::istringstream idx_stream(
        std::string(_data_buffer.data() + _index_offset, file_size - _index_offset - 12),
        std::ios::binary);
    uint32_t idx_count = read_u32(idx_stream);
    _index.clear();
    _index.reserve(idx_count);
    for (uint32_t i = 0; i < idx_count; ++i) {
        Key key = read_value(idx_stream);
        uint32_t off = read_u32(idx_stream);
        _index.emplace_back(key, off);
    }

    return true;
}

inline Record SSTable::get(const Key& key, bool& found) const {
    found = false;
    if (_index.empty()) return {};

    // binary search the index
    auto it = std::lower_bound(_index.begin(), _index.end(), key,
        [](const auto& entry, const Key& k) { return entry.first < k; });

    if (it == _index.end() || it->first != key) {
        // the key we want might be between index entries.  check the
        // entry just before the lower_bound result for a match.
        if (it != _index.begin()) --it;
        else return {};
    }

    // read the entry at the found offset
    auto [found_key, rec] = read_entry_at(it->second);
    if (found_key == key) {
        found = true;
        return rec;
    }
    return {};
}

template <typename Fn>
void SSTable::scan(const Key& start, const Key& end, Fn&& fn) const {
    if (_index.empty()) return;
    auto it = std::lower_bound(_index.begin(), _index.end(), start,
        [](const auto& entry, const Key& k) { return entry.first < k; });
    if (it != _index.begin()) --it;
    for (; it != _index.end(); ++it) {
        auto [key, rec] = read_entry_at(it->second);
        if (key > end) break;
        if (key >= start && key <= end) fn(key, rec);
    }
}

template <typename Fn>
void SSTable::for_each(Fn&& fn) const {
    for (const auto& idx_entry : _index) {
        auto [key, rec] = read_entry_at(idx_entry.second);
        fn(key, rec);
    }
}

inline std::pair<Key, Record> SSTable::read_entry_at(uint32_t offset) const {
    std::istringstream iss(
        std::string(_data_buffer.data() + offset, _data_buffer.size() - offset),
        std::ios::binary);
    Key key = read_value(iss);
    Record rec = read_record(iss);
    return {key, rec};
}

inline void SSTable::remove_file() {
    if (!_filepath.empty()) {
        std::remove(_filepath.c_str());
        _filepath.clear();
    }
}

} // namespace minidb
