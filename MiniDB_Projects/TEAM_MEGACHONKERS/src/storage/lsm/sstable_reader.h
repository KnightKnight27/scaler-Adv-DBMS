#pragma once

#include <string>
#include <optional>
#include "common/types.h"
#include "storage/lsm/memtable.h" // For SearchResult enum

namespace minidb {

class SSTableReader {
public:
    // Scans a specific SSTable file for a key.
    // Returns FOUND (and populates out_row), DELETED (if it hit a tombstone), or NOT_FOUND.
    static SearchResult FindKey(const std::string& file_path, const InternalKey& target_key, Row* out_row);

    // Reads all key-value pairs from an SSTable (Needed later for Compaction and SeqScan)
    static std::map<std::string, std::string> ReadAll(const std::string& file_path);
};

} // namespace minidb