#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>

#include "minidb/common/types.h"

namespace minidb {

class DiskManager {
 public:
  explicit DiskManager(std::filesystem::path path);
  ~DiskManager();

  DiskManager(const DiskManager &) = delete;
  DiskManager &operator=(const DiskManager &) = delete;

  PageId AllocatePage();
  void ReadPage(PageId page_id, std::span<std::byte, kPageSize> output);
  void WritePage(PageId page_id,
                 std::span<const std::byte, kPageSize> input);
  void Flush();
  std::size_t PageCount() const;
  const std::filesystem::path &Path() const { return path_; }

 private:
  std::filesystem::path path_;
  mutable std::mutex mutex_;
  std::fstream file_;
  PageId next_page_id_{0};
};

}  // namespace minidb
