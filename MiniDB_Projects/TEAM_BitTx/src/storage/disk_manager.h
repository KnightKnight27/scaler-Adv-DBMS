#pragma once

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace minidb {

using namespace std;

class DiskManager {
public:
  explicit DiskManager(const string& dbFile);
  ~DiskManager();

  bool IsOpen() const;
  int32_t GetNumPages() const;

  void ReadPage(int32_t pageId, char* data);
  void WritePage(int32_t pageId, const char* data);

  int32_t AllocatePage();
  void DeallocatePage(int32_t pageId);

  void Sync();

private:
  string dbFile_;
  fstream file_;
  int32_t numPages_ = 0;
  unordered_map<int32_t, int32_t> freeList_;
  mutable mutex latch_;
};

} // namespace minidb
