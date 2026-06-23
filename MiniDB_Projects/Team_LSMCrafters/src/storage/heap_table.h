#pragma once
#include "index/bplus_tree.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "storage/storage_engine.h"

namespace minidb {

// The default storage engine: rows live in a heap file and a B+Tree maps each
// primary key to the row's RID. Each heap record is the 8-byte key followed by
// the opaque serialized row, so the engine can recover the key during a scan
// without knowing the table schema. The B+Tree is rebuilt from the heap when
// the table is opened.
class HeapTable : public StorageEngine {
 public:
  HeapTable(BufferPool& buffer_pool, PageId first_page);

  void insert(Key key, const Bytes& value) override;  // insert or overwrite
  std::optional<Bytes> get(Key key) override;
  void erase(Key key) override;
  std::unique_ptr<RowCursor> scan() override;
  std::unique_ptr<RowCursor> index_range(Key lo, Key hi) override;
  bool supports_index_scan() const override { return true; }
  const TableStats& stats() const override { return stats_; }
  void flush() override { buffer_pool_.flush_all(); }

  PageId first_page() const { return heap_.first_page(); }  // for reopening after a crash

 private:
  void rebuild_index();        // populate the B+Tree + stats from the heap
  void note_key(Key key);      // extend min/max as keys are inserted

  BufferPool& buffer_pool_;
  HeapFile    heap_;
  BPlusTree   index_;
  TableStats  stats_;
};

// Allocates and formats a fresh first page, then returns an empty HeapTable
// backed by it. The single place that knows how to bootstrap a heap table.
std::unique_ptr<HeapTable> make_heap_table(BufferPool& buffer_pool);

}  // namespace minidb
