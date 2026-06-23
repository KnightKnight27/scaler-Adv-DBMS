#include "storage/heap_file.h"

#include "buffer/buffer_pool.h"
#include "storage/page.h"

#include <cstring>

namespace minidb {

using namespace std;

HeapFile::HeapFile(DiskManager* dm, BufferPool* bp) : dm_(dm), bp_(bp) {}

HeapFile::~HeapFile() {
  if (currentPageId_ >= 0) {
    WritePage(currentPageId_, &currentPage_);
  }
}

bool HeapFile::ReadPage(int32_t pageId, Page* page) const {
  if (bp_) {
    Page* cached = bp_->FetchPage(pageId);
    if (!cached)
      return false;
    *page = *cached;
    bp_->UnpinPage(pageId, false);
    return true;
  }
  char data[PAGE_SIZE];
  dm_->ReadPage(pageId, data);
  memcpy(page->GetData(), data, PAGE_SIZE);
  page->GetHeader() = *reinterpret_cast<PageHeader*>(data);
  return true;
}

bool HeapFile::WritePage(int32_t pageId, const Page* page) const {
  if (bp_) {
    Page* cached = bp_->FetchPage(pageId);
    if (!cached)
      return false;
    *cached = *page;
    bp_->UnpinPage(pageId, true);
    return true;
  }
  page->WriteHeader();
  dm_->WritePage(pageId, page->GetData());
  return true;
}

RecordId HeapFile::InsertTuple(const char* data, int32_t size) {
  int32_t numPages = dm_->GetNumPages();

  for (int32_t pid = 0; pid < numPages; ++pid) {
    Page page(pid);
    ReadPage(pid, &page);
    int32_t slot = page.InsertTuple(data, size);
    if (slot >= 0) {
      WritePage(pid, &page);
      currentPage_ = page;
      currentPageId_ = pid;
      numTuples_++;
      return RecordId(pid, slot);
    }
  }

  int32_t newPid = dm_->AllocatePage();
  Page page(newPid);
  page.Init(newPid);
  memset(page.GetData(), 0, PAGE_SIZE);
  int32_t slot = page.InsertTuple(data, size);
  WritePage(newPid, &page);
  currentPage_ = page;
  currentPageId_ = newPid;
  numTuples_++;
  return RecordId(newPid, slot);
}

bool HeapFile::DeleteTuple(const RecordId& rid) {
  if (!rid.IsValid())
    return false;
  Page page(rid.GetPageId());
  if (!ReadPage(rid.GetPageId(), &page))
    return false;
  bool ok = page.DeleteTuple(rid.GetSlotNum());
  if (ok) {
    WritePage(rid.GetPageId(), &page);
    currentPage_ = page;
    currentPageId_ = rid.GetPageId();
    numTuples_--;
  }
  return ok;
}

bool HeapFile::UpdateTuple(const RecordId& rid, const char* data, int32_t size) {
  if (!rid.IsValid())
    return false;
  Page page(rid.GetPageId());
  if (!ReadPage(rid.GetPageId(), &page))
    return false;
  bool ok = page.UpdateTuple(rid.GetSlotNum(), data, size);
  if (ok) {
    WritePage(rid.GetPageId(), &page);
    currentPage_ = page;
    currentPageId_ = rid.GetPageId();
  }
  return ok;
}

bool HeapFile::GetTuple(const RecordId& rid, const char*& data, int32_t& size) {
  if (!rid.IsValid())
    return false;
  if (currentPageId_ != rid.GetPageId()) {
    currentPage_ = Page(rid.GetPageId());
    ReadPage(rid.GetPageId(), &currentPage_);
    currentPageId_ = rid.GetPageId();
  }
  return currentPage_.GetTuple(rid.GetSlotNum(), data, size);
}

int32_t HeapFile::GetNumTuples() const {
  return numTuples_;
}

int32_t HeapFile::GetNumPages() const {
  return dm_->GetNumPages();
}

bool HeapFile::Iterator::operator!=(const Iterator& other) const {
  return !(*this == other);
}

HeapFile::Iterator& HeapFile::Iterator::operator++() {
  slotNum_++;
  if (file_ != nullptr) {
    Page page(pageId_);
    file_->ReadPage(pageId_, &page);
    while (slotNum_ >= page.GetNumSlots() && pageId_ < file_->dm_->GetNumPages()) {
      ++pageId_;
      slotNum_ = 0;
      if (pageId_ >= file_->dm_->GetNumPages())
        break;
      file_->ReadPage(pageId_, &page);
    }
  }
  return *this;
}

HeapFile::Iterator HeapFile::Begin() {
  return Iterator(this, 0, 0);
}

HeapFile::Iterator HeapFile::End() {
  return Iterator(this, dm_->GetNumPages(), 0);
}

} // namespace minidb