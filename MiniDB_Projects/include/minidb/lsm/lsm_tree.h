#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "minidb/lsm/memtable.h"
#include "minidb/lsm/sstable.h"

namespace minidb {

class LsmTree {
 public:
  explicit LsmTree(std::filesystem::path directory,
                   std::size_t flush_threshold = 1024);
  void Put(int key, std::string value);
  void Delete(int key);
  std::optional<std::string> Get(int key) const;
  void Flush();
  void Compact();
  std::size_t SSTableCount() const { return tables_.size(); }

 private:
  void LoadManifest();
  void SaveManifest() const;

  std::filesystem::path directory_;
  std::size_t flush_threshold_;
  std::uint64_t next_generation_{1};
  MemTable memtable_;
  std::vector<std::filesystem::path> tables_;  // oldest to newest
};

}  // namespace minidb
