// SSTable (Sorted String Table): an immutable, sorted on-disk run.
//
// File layout (all little-endian via memcpy):
//   [u64 count]
//   count * [u32 klen][key bytes][u8 tombstone][u32 vlen][val bytes]
// Entries are written in ascending key order. On open, a full in-memory index
// (key -> file offset) is built so point reads are a hash lookup + one seek.
// (A production system uses a sparse index + block cache; we keep it dense for
// clarity -- noted as a limitation.)
#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lsm/memtable.hpp"

namespace minidb {

class SSTable {
public:
    explicit SSTable(std::string path) : path_(std::move(path)) {}

    // Write a MemTable's sorted contents to a new SSTable file.
    static void build(const std::string& path,
                      const std::map<std::string, MemEntry>& entries);

    // Write an already-sorted merged stream to a new SSTable file.
    static void build_from_vector(
        const std::string& path,
        const std::vector<std::pair<std::string, MemEntry>>& entries);

    void open();   // load the dense key index

    // Look up a key. Returns the entry (possibly a tombstone) or nullopt if the
    // key is absent from *this* SSTable.
    std::optional<MemEntry> get(const std::string& key);

    // Iterate every entry in sorted order (used by compaction).
    std::vector<std::pair<std::string, MemEntry>> scan();

    const std::string& path() const { return path_; }
    size_t count() const { return index_.size(); }

private:
    std::ifstream& reader();   // lazily-opened, cached read stream

    std::string path_;
    std::map<std::string, uint64_t> index_;   // key -> byte offset of its entry
    std::unique_ptr<std::ifstream> reader_;    // kept open to avoid per-get open()
};

}  // namespace minidb
