#include "storage_lsm/compaction.h"
#include "storage_lsm/sstable.h"
#include "storage_lsm/memtable.h"

namespace minidb {

void Compaction::compact(const std::string& input1, const std::string& input2, const std::string& output) {
    // Simplest merge: read both, put in one map (newer overrides older). 
    // Here we'll assume input2 is newer.
    auto d1 = SSTable::read_all(input1);
    auto d2 = SSTable::read_all(input2);
    
    for (const auto& [k, v] : d2) {
        d1[k] = v;
    }
    
    MemTable merged;
    for (const auto& [k, v] : d1) {
        merged.put(k, v);
    }
    
    SSTable::write_from_memtable(output, merged);
}

} // namespace minidb
