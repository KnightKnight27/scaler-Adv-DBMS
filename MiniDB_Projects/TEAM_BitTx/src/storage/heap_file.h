#pragma once

#include "common/rid.h"
#include "storage/disk_manager.h"
#include "storage/page.h"

#include <string>

namespace minidb {

using namespace std;

class BufferPool;

class HeapFile {
public:
  HeapFile(DiskManager* dm, BufferPool* bp = nullptr);
  ~HeapFile();

  RecordId InsertTuple(const char* data, int32_t size);
  bool DeleteTuple(const RecordId& rid);
  bool UpdateTuple(const RecordId& rid, const char* data, int32_t size);
  bool GetTuple(const RecordId& rid, const char*& data, int32_t& size);

  int32_t GetNumTuples() const;
  int32_t GetNumPages() const;

  class Iterator {
  public:
    explicit Iterator(HeapFile* file, int32_t pageId, int32_t slotNum)
        : file_(file), pageId_(pageId), slotNum_(slotNum) {}

    bool operator!=(const Iterator& other) const;
    bool operator==(const Iterator& other) const {
      return pageId_ == other.pageId_ && slotNum_ == other.slotNum_;
    }

    Iterator& operator++();

    RecordId GetRid() {
      return RecordId(pageId_, slotNum_);
    }

  private:
    HeapFile* file_;
    int32_t pageId_;
    int32_t slotNum_;
  };

  Iterator Begin();
  Iterator End();

private:
  bool ReadPage(int32_t pageId, Page* page) const;
  bool WritePage(int32_t pageId, const Page* page) const;

  DiskManager* dm_;
  BufferPool* bp_;
  mutable Page currentPage_;
  mutable int32_t currentPageId_ = -1;
  int32_t numTuples_ = 0;
};

} // namespace minidb
