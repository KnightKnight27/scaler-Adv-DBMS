#include "buffer/buffer_pool.h"

#include <cstring>

namespace minidb {

using namespace std;

BufferPool::BufferPool(DiskManager* dm) : dm_(dm) {
  for (int32_t i = 0; i < POOL_SIZE; ++i) {
    lru_.push_back(i);
    lruIndex_[i] = prev(lru_.end());
  }
}

BufferPool::~BufferPool() {
  FlushAll();
}

Page* BufferPool::FetchPage(int32_t pageId) {
  if (pageId < 0 || pageId >= dm_->GetNumPages())
    return nullptr;
  lock_guard<mutex> lock(mu_);
  for (int32_t i = 0; i < POOL_SIZE; ++i) {
    if (frames_[i].page.GetPageId() == pageId) {
      frames_[i].pinCount++;
      lru_.erase(lruIndex_[i]);
      lru_.push_front(i);
      lruIndex_[i] = lru_.begin();
      return &frames_[i].page;
    }
  }
  int32_t frameId;
  if (!Evict(&frameId))
    return nullptr;
  Frame& f = frames_[frameId];
  if (f.dirty) {
    char data[PAGE_SIZE];
    memcpy(data, f.page.GetData(), PAGE_SIZE);
    dm_->WritePage(f.page.GetPageId(), data);
  }
  dm_->ReadPage(pageId, f.page.GetData());
  f.page.GetHeader() = *reinterpret_cast<PageHeader*>(f.page.GetData());
  f.page.Reset(pageId);
  f.dirty = false;
  f.pinCount = 1;
  lru_.erase(lruIndex_[frameId]);
  lru_.push_front(frameId);
  lruIndex_[frameId] = lru_.begin();
  return &f.page;
}

bool BufferPool::UnpinPage(int32_t pageId, bool dirty) {
  lock_guard<mutex> lock(mu_);
  for (int32_t i = 0; i < POOL_SIZE; ++i) {
    if (frames_[i].page.GetPageId() == pageId && frames_[i].pinCount > 0) {
      if (dirty)
        frames_[i].dirty = true;
      frames_[i].pinCount--;
      return true;
    }
  }
  return false;
}

bool BufferPool::FlushPage(int32_t pageId) {
  lock_guard<mutex> lock(mu_);
  for (int32_t i = 0; i < POOL_SIZE; ++i) {
    if (frames_[i].page.GetPageId() == pageId) {
      if (frames_[i].pinCount > 0)
        return false;
      if (frames_[i].dirty) {
        dm_->WritePage(pageId, frames_[i].page.GetData());
        frames_[i].dirty = false;
      }
      return true;
    }
  }
  return false;
}

bool BufferPool::FlushAll() {
  for (int32_t i = 0; i < POOL_SIZE; ++i) {
    int32_t pid = frames_[i].page.GetPageId();
    if (pid >= 0 && frames_[i].dirty) {
      FlushPage(pid);
    }
  }
  return true;
}

bool BufferPool::Evict(int32_t* outFrameId) {
  for (auto it = lru_.rbegin(); it != lru_.rend(); ++it) {
    int32_t id = *it;
    if (frames_[id].pinCount == 0) {
      *outFrameId = id;
      return true;
    }
  }
  return false;
}

} // namespace minidb