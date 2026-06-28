#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>

// ============================================================
// SSTable — Sorted String Table (immutable on-disk file)
//
// Format:
//   [entry_count(4B)]
//   entries[], each = [key(4B)][deleted(1B)][data_len(4B)][data...]
//
// Entries are sorted by key. Supports binary search for lookup.
// ============================================================

struct SSEntry {
    int key;
    bool deleted;
    std::vector<char> data;
};

class SSTable {
public:
    // Create an SSTable by writing entries to disk
    static void Write(const std::string& filename,
                      const std::vector<SSEntry>& entries);

    // Open an existing SSTable from disk
    explicit SSTable(const std::string& filename);

    // Lookup a key. Returns nullptr if not found.
    const SSEntry* Get(int key) const;

    // Get all entries
    const std::vector<SSEntry>& GetEntries() const { return entries_; }

    // Number of entries
    int Size() const { return entries_.size(); }

    // File path
    const std::string& GetPath() const { return path_; }

    // Merge multiple SSTables into one (compaction)
    static void Merge(const std::vector<SSTable*>& tables,
                      const std::string& output_file);

private:
    std::string path_;
    std::vector<SSEntry> entries_;  // loaded into memory for simplicity
};
