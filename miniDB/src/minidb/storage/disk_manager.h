#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>

#include "minidb/storage/page.h"

namespace minidb {

class DiskManager {
 public:
  explicit DiskManager(std::filesystem::path path);
  ~DiskManager();

  PageId AllocatePage();
  void ReadPage(PageId page_id, Page& page);
  void WritePage(const Page& page);
  PageId PageCount() const;
  void Flush();
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
  mutable std::mutex mutex_;
  std::fstream file_;
};

}  // namespace minidb
