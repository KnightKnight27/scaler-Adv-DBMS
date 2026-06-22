#include "storage/heap_file.h"

#include <cstring>

namespace minidb {

namespace {
const int kHeaderSize = 8;

PageId GetNext(const char* p) {
  int32_t v; std::memcpy(&v, p, 4); return v;
}
void SetNext(char* p, PageId v) { std::memcpy(p, &v, 4); }

void SetRecSize(char* p, int16_t v) { std::memcpy(p + 4, &v, 2); }

int16_t GetNumUsed(const char* p) {
  int16_t v; std::memcpy(&v, p + 6, 2); return v;
}
void SetNumUsed(char* p, int16_t v) { std::memcpy(p + 6, &v, 2); }

void InitPage(char* p, int record_size) {
  std::memset(p, 0, PAGE_SIZE);
  SetNext(p, INVALID_PAGE_ID);
  SetRecSize(p, static_cast<int16_t>(record_size));
  SetNumUsed(p, 0);
}
}  // namespace

int HeapFile::SlotsPerPage() const {
  return (PAGE_SIZE - kHeaderSize) / (1 + record_size_);
}

PageId HeapFile::CreateNew(BufferPool* bp, int record_size) {
  PageId pid;
  Frame* f = bp->NewPage(&pid);
  InitPage(f->data, record_size);
  bp->UnpinPage(pid, true);
  return pid;
}

RID HeapFile::Insert(const char* record) {
  const int slot_stride = 1 + record_size_;
  const int slots = SlotsPerPage();

  PageId pid = first_page_;
  PageId last = first_page_;
  while (pid != INVALID_PAGE_ID) {
    Frame* f = bp_->FetchPage(pid);
    char* p = f->data;
    for (int i = 0; i < slots; ++i) {
      char* slot = p + kHeaderSize + i * slot_stride;
      if (slot[0] == 0) {
        slot[0] = 1;
        std::memcpy(slot + 1, record, record_size_);
        SetNumUsed(p, static_cast<int16_t>(GetNumUsed(p) + 1));
        bp_->UnpinPage(pid, true);
        return RID(pid, static_cast<int16_t>(i));
      }
    }
    last = pid;
    PageId next = GetNext(p);
    bp_->UnpinPage(pid, false);
    pid = next;
  }

  // No free slot anywhere: append a new page and link it in.
  PageId np;
  Frame* nf = bp_->NewPage(&np);
  char* p = nf->data;
  InitPage(p, record_size_);
  char* slot = p + kHeaderSize;  // slot 0
  slot[0] = 1;
  std::memcpy(slot + 1, record, record_size_);
  SetNumUsed(p, 1);
  bp_->UnpinPage(np, true);

  Frame* lf = bp_->FetchPage(last);
  SetNext(lf->data, np);
  bp_->UnpinPage(last, true);

  return RID(np, 0);
}

bool HeapFile::Get(RID rid, char* out) {
  Frame* f = bp_->FetchPage(rid.page);
  char* slot = f->data + kHeaderSize + rid.slot * (1 + record_size_);
  bool live = slot[0] == 1;
  if (live) std::memcpy(out, slot + 1, record_size_);
  bp_->UnpinPage(rid.page, false);
  return live;
}

bool HeapFile::Update(RID rid, const char* record) {
  Frame* f = bp_->FetchPage(rid.page);
  char* slot = f->data + kHeaderSize + rid.slot * (1 + record_size_);
  bool live = slot[0] == 1;
  if (live) std::memcpy(slot + 1, record, record_size_);
  bp_->UnpinPage(rid.page, live);
  return live;
}

bool HeapFile::Delete(RID rid) {
  Frame* f = bp_->FetchPage(rid.page);
  char* p = f->data;
  char* slot = p + kHeaderSize + rid.slot * (1 + record_size_);
  bool live = slot[0] == 1;
  if (live) {
    slot[0] = 0;
    SetNumUsed(p, static_cast<int16_t>(GetNumUsed(p) - 1));
  }
  bp_->UnpinPage(rid.page, live);
  return live;
}

void HeapFile::Scan(const std::function<void(RID, const char*)>& visitor) {
  const int slot_stride = 1 + record_size_;
  const int slots = SlotsPerPage();
  PageId pid = first_page_;
  while (pid != INVALID_PAGE_ID) {
    Frame* f = bp_->FetchPage(pid);
    char* p = f->data;
    for (int i = 0; i < slots; ++i) {
      char* slot = p + kHeaderSize + i * slot_stride;
      if (slot[0] == 1) {
        visitor(RID(pid, static_cast<int16_t>(i)), slot + 1);
      }
    }
    PageId next = GetNext(p);
    bp_->UnpinPage(pid, false);
    pid = next;
  }
}

}  // namespace minidb
