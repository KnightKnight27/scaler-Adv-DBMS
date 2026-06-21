#pragma once

#include <memory>
#include <string>

#include "buffer/buffer_pool.h"
#include "engine/storage_engine.h"
#include "index/bplus_tree.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"

namespace walterdb {

// ===========================================================================
// HeapBTreeEngine -- the "classic" storage engine exposed through the shared KV
// StorageEngine interface, so it can be A/B benchmarked against the LSM engine.
//
// It is exactly the required core primitives wired as a key-value store:
//   * values live in a slotted-page HEAP FILE (addressed by RID),
//   * a B+TREE maps key -> RID (point lookups and ordered range scans),
//   * everything goes through the buffer pool.
//
// put  = (find old RID, tombstone it) + heap insert + index upsert
// get  = index search -> RID -> heap fetch
// scan = index range -> RIDs -> heap fetches, in key order
//
// A small header page (page 0) records the heap's first page and the index's
// meta page so the store reopens cleanly.
// ===========================================================================
class HeapBTreeEngine : public StorageEngine {
 public:
  explicit HeapBTreeEngine(const std::string& path, size_t buffer_frames = 4096);
  ~HeapBTreeEngine() override;

  Status put(std::string_view key, std::string_view value) override;
  std::optional<std::string> get(std::string_view key) override;
  Status remove(std::string_view key) override;
  std::unique_ptr<KVIterator> scan(std::string_view lo, std::string_view hi) override;
  void flush() override;
  std::string name() const override { return "HeapBTree"; }

  uint64_t disk_size() const;                              // data file size (storage amplification)
  uint64_t bytes_written() const { return disk_.bytes_written(); }  // (write amplification)

 private:
  DiskManager disk_;
  BufferPool pool_;
  std::unique_ptr<HeapFile> heap_;
  std::unique_ptr<BPlusTree> index_;
};

}  // namespace walterdb
