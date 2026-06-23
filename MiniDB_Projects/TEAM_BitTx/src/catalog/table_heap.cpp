#include "catalog/table_heap.h"

#include "buffer/buffer_pool.h"
#include "storage/disk_manager.h"

#include <vector>

namespace minidb {

using namespace std;

TableHeap::TableHeap(DiskManager* dm, const Schema& schema, BufferPool* bp)
    : schema_(schema) {
  if (bp == nullptr) {
    localBp_ = make_unique<BufferPool>(dm);
    bp = localBp_.get();
  }
  heap_ = make_unique<HeapFile>(dm, bp);
}

TableHeap::~TableHeap() = default;

bool TableHeap::InsertTuple(const Tuple& tuple, RecordId* outRid) {
  size_t size = sizeof(int32_t);
  for (const Value& v : tuple.GetValues()) {
    size += v.SerializedSize();
  }
  vector<char> buf(size);
  char* p = buf.data();
  tuple.SerializeTo(p);
  RecordId rid = heap_->InsertTuple(buf.data(), static_cast<int32_t>(size));
  if (!rid.IsValid())
    return false;
  if (outRid)
    *outRid = rid;
  return true;
}

bool TableHeap::DeleteTuple(const RecordId& rid) {
  return heap_->DeleteTuple(rid);
}

bool TableHeap::UpdateTuple(const RecordId& rid, const Tuple& tuple) {
  size_t size = sizeof(int32_t);
  for (const Value& v : tuple.GetValues()) {
    size += v.SerializedSize();
  }
  vector<char> buf(size);
  char* p = buf.data();
  tuple.SerializeTo(p);
  return heap_->UpdateTuple(rid, buf.data(), static_cast<int32_t>(size));
}

bool TableHeap::GetTuple(const RecordId& rid, Tuple* tuple) {
  const char* data = nullptr;
  int32_t size = 0;
  if (!heap_->GetTuple(rid, data, size))
    return false;
  *tuple = Tuple::DeserializeFrom(data, schema_);
  tuple->SetRid(rid);
  return true;
}

int32_t TableHeap::GetNumTuples() const {
  return heap_->GetNumTuples();
}

int32_t TableHeap::GetNumPages() const {
  return heap_->GetNumPages();
}

const Schema& TableHeap::GetSchema() const {
  return schema_;
}

} // namespace minidb