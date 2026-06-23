#pragma once
#include <map>
#include <string>

namespace minidb {

class MemTable {
public:
    void put(int key, const std::string& value);
    bool get(int key, std::string& value);
    
    const std::map<int, std::string>& get_data() const { return data_; }
    void clear() { data_.clear(); }
    size_t size() const { return data_.size(); }

private:
    std::map<int, std::string> data_;
};

} // namespace minidb
