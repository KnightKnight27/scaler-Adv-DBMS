#include "storage/lsm_row_store.h"
#include <stdexcept>

namespace minidb {

void LSMRowStore::Insert(int64_t key, const Tuple &row) {
  // Enforce primary-key uniqueness (the Bloom filters make this check cheap for
  // absent keys, so blind sequential inserts stay fast).
  std::string existing;
  if (tree_.Get(key, &existing)) {
    throw std::runtime_error("duplicate primary key: " + std::to_string(key));
  }
  tree_.Put(key, row.Serialize(schema_));
}

bool LSMRowStore::Get(int64_t key, Tuple *out) {
  std::string bytes;
  if (!tree_.Get(key, &bytes)) return false;
  *out = Tuple::Deserialize(schema_, bytes);
  return true;
}

bool LSMRowStore::Delete(int64_t key) {
  std::string bytes;
  if (!tree_.Get(key, &bytes)) return false;  // nothing to delete
  tree_.Delete(key);
  return true;
}

namespace {
// Cursor over a materialized, key-ordered list of serialized rows.
class VectorCursor : public RowCursor {
 public:
  VectorCursor(std::vector<std::pair<int64_t, std::string>> rows, const Schema &schema)
      : rows_(std::move(rows)), schema_(schema) {}
  bool Next(Tuple *out) override {
    if (pos_ >= rows_.size()) return false;
    *out = Tuple::Deserialize(schema_, rows_[pos_++].second);
    return true;
  }

 private:
  std::vector<std::pair<int64_t, std::string>> rows_;
  const Schema                                &schema_;
  size_t                                       pos_{0};
};
}  // namespace

std::unique_ptr<RowCursor> LSMRowStore::ScanAll() {
  return std::make_unique<VectorCursor>(tree_.ScanAll(), schema_);
}

std::unique_ptr<RowCursor> LSMRowStore::RangeScan(int64_t low, int64_t high) {
  return std::make_unique<VectorCursor>(tree_.Range(low, high), schema_);
}

}  // namespace minidb
