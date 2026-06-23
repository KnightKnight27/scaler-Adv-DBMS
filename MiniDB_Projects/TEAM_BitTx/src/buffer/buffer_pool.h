#pragma once

#include "storage/disk_manager.h"
#include "storage/page.h"

#include <array>
#include <cstdint>
#include <list>
#include <mutex>
#include <unordered_map>

namespace minidb {

using namespace std;

class BufferPool {
public:
  static constexpr int32_t POOL_SIZE = 32;

  struct Frame {
    Page page;
    bool dirty = false;
    int32_t pinCount = 0;
  };

  BufferPool(DiskManager* dm);
  ~BufferPool();

  Page* FetchPage(int32_t pageId);
  bool UnpinPage(int32_t pageId, bool dirty);
  bool FlushPage(int32_t pageId);
  bool FlushAll();

private:
  bool Evict(int32_t* outFrameId);

  DiskManager* dm_;
  array<Frame, POOL_SIZE> frames_;
  list<int32_t> lru_;
  unordered_map<int32_t, list<int32_t>::iterator> lruIndex_;
  mutex mu_;
};

} // namespace minidb