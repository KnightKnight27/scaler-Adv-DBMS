#pragma once
// Baseline storage engine: slotted-page heap files over a buffer pool.
#include "minidb/catalog.hpp"
#include "minidb/storage/buffer_pool.hpp"
#include "minidb/storage/index.hpp"
#include "minidb/storage/page_manager.hpp"
#include "minidb/storage/storage_engine.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace minidb {

class HeapEngine : public StorageEngine {
 public:
  HeapEngine(Catalog& catalog, const std::string& data_path, size_t buffer_frames = 64);

  RID insert(TableId table, const Tuple& t) override;
  bool remove(TableId table, const RID& rid) override;
  Optional<Tuple> get(TableId table, const RID& rid) override;
  Optional<Tuple> find(TableId table, const Value& key) override;
  std::vector<RID> index_lookup(TableId table, size_t column, const Value& key) override;
  std::unique_ptr<RecordIterator> scan(TableId table) override;
  void replay_insert(TableId table, const RID& rid, const std::vector<uint8_t>& tuple_bytes) override;
  void replay_delete(TableId table, const RID& rid) override;
  void flush() override;

  BufferPool& buffer_pool() { return pool_; }

  // Builds (or rebuilds) one index by scanning its table. Called after
  // CREATE INDEX and during construction for every catalog index.
  void build_index(IndexId id);
  // The B+Tree backing an index on (table, column), or nullptr if none exists.
  const BPlusTree* index_for(TableId table, size_t column) const;

 private:
  void rebuild_all_indexes();
  void index_insert(const TableMeta& meta, const Tuple& t, const RID& rid);
  void index_remove(const TableMeta& meta, const Tuple& t, const RID& rid);

  Catalog& catalog_;
  PageManager pm_;
  BufferPool pool_;
  std::unordered_map<IndexId, BPlusTree> indexes_;  // keyed by index id
};

}  // namespace minidb
