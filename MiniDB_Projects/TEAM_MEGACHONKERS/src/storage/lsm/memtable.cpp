#include "storage/lsm/memtable.h"
#include <mutex>

namespace minidb {

void MemTable::Put(const InternalKey& key, const Row& row) {
    std::string encoded_key = key.Encode();
    std::string serialized_row = row.Serialize();
    
    // Exclusive lock: only one thread can mutate the map at a time
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = table_.find(encoded_key);
    if (it == table_.end()) {
        current_size_ += encoded_key.size() + serialized_row.size();
    } else {
        // Adjust size tracking if we are overwriting an existing key
        current_size_ += serialized_row.size() - it->second.size();
    }
    
    table_[encoded_key] = serialized_row;
}

void MemTable::Delete(const InternalKey& key) {
    std::string encoded_key = key.Encode();
    std::string tombstone = ""; // An empty string represents our tombstone marker
    
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = table_.find(encoded_key);
    if (it == table_.end()) {
        current_size_ += encoded_key.size();
    } else {
        current_size_ -= it->second.size();
    }
    
    table_[encoded_key] = tombstone;
}

SearchResult MemTable::Get(const InternalKey& key, Row* out_row) const {
    std::string encoded_key = key.Encode();
    
    // Shared lock: multiple threads can read simultaneously
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    
    auto it = table_.find(encoded_key);
    if (it == table_.end()) {
        return SearchResult::NOT_FOUND;
    }
    
    // Check for Tombstone
    if (it->second.empty()) {
        return SearchResult::DELETED;
    }
    
    // If the caller provided a pointer, deserialize the payload into it
    if (out_row != nullptr) {
        *out_row = Row::Deserialize(it->second);
    }
    
    return SearchResult::FOUND;
}

bool MemTable::NeedsFlush() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return current_size_ >= MEMTABLE_MAX_SIZE;
}

std::map<std::string, std::string> MemTable::GetAllEntries() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return table_; 
}

void MemTable::Clear() {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    table_.clear();
    current_size_ = 0;
}

} // namespace minidb