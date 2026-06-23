#pragma once

#include <map>
#include <shared_mutex>
#include <string>
#include <optional>
#include "common/types.h"
#include "common/config.h"

namespace minidb {

// The three states of an LSM lookup
enum class SearchResult {
    FOUND,
    DELETED,
    NOT_FOUND
};

class MemTable {
private:
    // We store encoded byte arrays instead of 'Row' objects. 
    // This allows us to dump memory straight to disk without re-serializing.
    std::map<std::string, std::string> table_;
    
    // Read-Write lock for concurrent access
    mutable std::shared_mutex rw_mutex_;
    
    size_t current_size_{0};

public:
    MemTable() = default;

    // Core operations
    void Put(const InternalKey& key, const Row& row);
    void Delete(const InternalKey& key);
    SearchResult Get(const InternalKey& key, Row* out_row) const;
    
    // Lifecycle management
    bool NeedsFlush() const;
    void Clear();
    
    // Expose entries for the background flushing thread
    std::map<std::string, std::string> GetAllEntries() const;
};

} // namespace minidb