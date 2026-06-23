#pragma once

#include <functional>
#include <vector>

#include "common.h"
#include "storage.h"
#include "wal.h"

namespace minidb {

class TableHeap {
 public:
  TableHeap(BufferPool* bpool, DiskManager* dm, LogManager* log, file_id_t fid, Schema schema)
      : bpool_(bpool), dm_(dm), log_(log), fid_(fid), schema_(std::move(schema)) {}

  const Schema& schema() const { return schema_; }
  file_id_t file_id() const { return fid_; }

  RID insert(txn_id_t txn, const Tuple& tuple);

  bool get(RID rid, Tuple* out) const;

  void remove(txn_id_t txn, RID rid);

  // callback returns false to stop early
  void scan(const std::function<bool(RID, const Tuple&)>& fn) const;

 private:
  BufferPool* bpool_;
  DiskManager* dm_;
  LogManager* log_;
  file_id_t fid_;
  Schema schema_;
};

}  // namespace minidb
