#include "storage_lsm/memtable.h"

namespace minidb {

void MemTable::put(int key, const std::string& value) {
    data_[key] = value;
}

bool MemTable::get(int key, std::string& value) {
    auto it = data_.find(key);
    if (it != data_.end()) {
        value = it->second;
        return true;
    }
    return false;
}

} // namespace minidb
