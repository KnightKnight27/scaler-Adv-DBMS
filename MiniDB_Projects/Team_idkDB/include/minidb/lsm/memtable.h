#pragma once

#include <map>
#include <optional>
#include <string>

namespace minidb {

class MemTable {
 public:
  void Put(int key, std::string value) { entries_[key] = std::move(value); }
  void Delete(int key) { entries_[key] = std::nullopt; }
  std::optional<std::optional<std::string>> Get(int key) const {
    auto it = entries_.find(key);
    return it == entries_.end()
               ? std::nullopt
               : std::optional<std::optional<std::string>>(it->second);
  }
  std::size_t Size() const { return entries_.size(); }
  const std::map<int, std::optional<std::string>> &Entries() const {
    return entries_;
  }
  void Clear() { entries_.clear(); }

 private:
  std::map<int, std::optional<std::string>> entries_;
};

}  // namespace minidb
