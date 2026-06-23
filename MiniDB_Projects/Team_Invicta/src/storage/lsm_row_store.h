#pragma once
#include <string>
#include "lsm/lsm_tree.h"
#include "storage/row_store.h"

namespace minidb {

// A RowStore backed by the LSM tree (Track C). Tuples are serialized to bytes
// and stored under their primary key; deletes write tombstones. Because it
// implements the same RowStore interface as HeapRowStore, the SQL parser,
// optimizer, and executor run over it unchanged — selected per table with
// `CREATE TABLE ... USING LSM`.
class LSMRowStore : public RowStore {
 public:
  LSMRowStore(const std::string &dir, Schema schema, size_t mem_limit = (1u << 20),
              size_t compaction_trigger = 4)
      : schema_(std::move(schema)), tree_(dir, mem_limit, compaction_trigger) {}

  void Insert(int64_t key, const Tuple &row) override;
  bool Get(int64_t key, Tuple *out) override;
  bool Delete(int64_t key) override;
  std::unique_ptr<RowCursor> ScanAll() override;
  std::unique_ptr<RowCursor> RangeScan(int64_t low, int64_t high) override;
  size_t RowCount() override { return tree_.LiveCount(); }
  bool KeyRange(int64_t *min_key, int64_t *max_key) override { return tree_.KeyRange(min_key, max_key); }
  void Sync() override { tree_.Flush(); }
  const Schema &schema() const override { return schema_; }

  LSMTree &tree() { return tree_; }  // for benchmarks / direct access

 private:
  Schema  schema_;
  LSMTree tree_;
};

}  // namespace minidb
