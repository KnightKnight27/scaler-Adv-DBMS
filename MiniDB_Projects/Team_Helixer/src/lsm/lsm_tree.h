#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace minidb {

// ---------------------------------------------------------------------------
// Extension Track C: a Log-Structured Merge tree as an alternative storage
// engine, contrasted with the page-based B+Tree.
//
// Design (the three classic LSM pieces):
//   - MemTable : an in-memory sorted map of recent writes. Writes are O(log n)
//                in memory with NO random disk I/O, which is the LSM write-win.
//   - SSTable  : when the MemTable fills, it is flushed as an immutable,
//                sorted run written sequentially to disk.
//   - Compaction: SSTables accumulate; compaction merges them into one run,
//                discarding overwritten keys and tombstones (reclaims space,
//                speeds reads).
//
// Keys are int32; values are byte strings. Deletes write a tombstone. A read
// checks the MemTable, then SSTables newest -> oldest, returning the first hit.
// ---------------------------------------------------------------------------

struct LSMEntry {
    int32_t     key;
    bool        tombstone;  // true => this key is deleted
    std::string value;
};

// An immutable on-disk sorted run. The sorted entries are cached in memory for
// fast (binary-search) reads, mirroring a real SSTable's block cache.
class SSTable {
public:
    // Persist `sorted` (ascending by key) to `path` and return the handle.
    static SSTable create(const std::string &path, const std::vector<LSMEntry> &sorted);
    // Load an existing SSTable file from disk.
    static SSTable open(const std::string &path);

    bool get(int32_t key, LSMEntry *out) const; // binary search
    const std::vector<LSMEntry> &entries() const { return entries_; }
    size_t disk_bytes() const { return bytes_; }
    const std::string &path() const { return path_; }

private:
    std::string           path_;
    std::vector<LSMEntry> entries_; // sorted, cached in memory
    size_t                bytes_{0};
};

class LSMTree {
public:
    explicit LSMTree(const std::string &dir, size_t memtable_limit = 50000);

    void put(int32_t key, const std::string &value);
    void remove(int32_t key);
    bool get(int32_t key, std::string *out); // false if absent or deleted

    void flush();    // MemTable -> new SSTable
    void compact();  // merge all SSTables into a single run

    // ---- statistics for benchmarking ----
    size_t num_sstables() const { return ssts_.size(); }
    size_t memtable_size() const { return mem_.size(); }
    size_t total_disk_bytes() const;

private:
    std::string                  dir_;
    size_t                       limit_;
    int                          next_id_{0};
    std::map<int32_t, LSMEntry>  mem_;   // MemTable (ordered)
    std::vector<SSTable>         ssts_;  // newest at the back
};

} // namespace minidb
