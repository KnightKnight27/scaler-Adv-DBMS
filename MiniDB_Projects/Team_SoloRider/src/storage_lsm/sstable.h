#pragma once
#include "storage_lsm/memtable.h"
#include <string>
#include <vector>

namespace minidb {

class SSTable {
public:
    // Write an SSTable from a memtable
    static void write_from_memtable(const std::string& path, const MemTable& memtable);
    
    // Read an SSTable sequentially
    static std::map<int, std::string> read_all(const std::string& path);
    
    // Search for a key in an SSTable
    static bool get(const std::string& path, int key, std::string& value);
};

} // namespace minidb
