#pragma once

#include "common/rid.h"
#include "common/tuple.h"
#include "common/types.h"
#include "storage/heap_file.h"

#include <memory>
#include <string>

namespace minidb {

using namespace std;

class BufferPool;

class TableHeap {
public:
  TableHeap(DiskManager* dm, const Schema& schema, BufferPool* bp = nullptr);
  ~TableHeap();

  bool InsertTuple(const Tuple& tuple, RecordId* outRid);
  bool DeleteTuple(const RecordId& rid);
  bool UpdateTuple(const RecordId& rid, const Tuple& tuple);
  bool GetTuple(const RecordId& rid, Tuple* tuple);

  int32_t GetNumTuples() const;
  int32_t GetNumPages() const;
  const Schema& GetSchema() const;

  HeapFile* GetHeap() const {
    return heap_.get();
  }

private:
  Schema schema_;
  unique_ptr<BufferPool> localBp_;
  unique_ptr<HeapFile> heap_;
};

} // namespace minidb