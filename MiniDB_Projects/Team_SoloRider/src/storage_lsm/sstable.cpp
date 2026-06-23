#include "storage_lsm/sstable.h"
#include <fstream>
#include <iostream>

namespace minidb {

void SSTable::write_from_memtable(const std::string& path, const MemTable& memtable) {
    std::ofstream out(path, std::ios::binary);
    for (const auto& [k, v] : memtable.get_data()) {
        out.write(reinterpret_cast<const char*>(&k), sizeof(k));
        size_t len = v.size();
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(v.data(), len);
    }
}

std::map<int, std::string> SSTable::read_all(const std::string& path) {
    std::map<int, std::string> result;
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return result;
    
    while (in.peek() != EOF) {
        int k;
        in.read(reinterpret_cast<char*>(&k), sizeof(k));
        if (in.eof()) break;
        
        size_t len;
        in.read(reinterpret_cast<char*>(&len), sizeof(len));
        
        std::string v(len, '\0');
        in.read(&v[0], len);
        result[k] = v;
    }
    return result;
}

bool SSTable::get(const std::string& path, int key, std::string& value) {
    // For simplicity, just read all (in memory lookup). 
    // Real SSTable would binary search an index block or scan linearly if no index.
    auto data = read_all(path);
    auto it = data.find(key);
    if (it != data.end()) {
        value = it->second;
        return true;
    }
    return false;
}

} // namespace minidb
