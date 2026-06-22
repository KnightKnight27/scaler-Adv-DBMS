#include "catalog/table.h"

namespace axiomdb {

Table::Table(BufferPoolManager* bpm, TableInfo* info) : bpm_(bpm), info_(info) {
  heap_ = std::make_unique<HeapTable>(bpm, info->heap_first_page);
  if (info->has_primary_index()) {
    index_ = std::make_unique<BPlusTreeIndex>(bpm, info->index_meta_page);
  }
}

Status Table::insert(const Tuple& tuple, RID* out_rid) {
  // Enforce primary-key uniqueness before writing anything.
  if (index_) {
    const Value& key = tuple.value(static_cast<size_t>(info_->pk_column));
    if (key.is_null()) return Status::invalid_argument("primary key cannot be NULL");
    if (index_->search(key.encode_key()).has_value()) {
      return Status::already_exists("duplicate primary key " + key.to_string());
    }
  }

  std::string bytes = tuple.encode(schema());
  RID rid = heap_->insert(bytes);

  if (index_) {
    const Value& key = tuple.value(static_cast<size_t>(info_->pk_column));
    index_->insert(key.encode_key(), rid);
  }
  ++info_->row_count;
  if (out_rid) *out_rid = rid;
  return {};
}

bool Table::erase(RID rid) {
  auto tuple = get(rid);
  if (!tuple) return false;
  if (index_) {
    const Value& key = tuple->value(static_cast<size_t>(info_->pk_column));
    index_->erase(key.encode_key());
  }
  bool ok = heap_->erase(rid);
  if (ok && info_->row_count > 0) --info_->row_count;
  return ok;
}

std::optional<Tuple> Table::get(RID rid) const {
  auto bytes = heap_->get(rid);
  if (!bytes) return std::nullopt;
  return Tuple::decode(schema(), *bytes);
}

std::optional<RID> Table::lookup_pk(const Value& key) const {
  if (!index_) return std::nullopt;
  return index_->search(key.encode_key());
}

Status Table::upsert(const Tuple& tuple, RID* out_rid) {
  if (index_) {
    const Value& key = tuple.value(static_cast<size_t>(info_->pk_column));
    if (key.is_null()) return Status::invalid_argument("primary key cannot be NULL");
    if (auto existing = index_->search(key.encode_key())) {
      // Replace in place: tombstone the old heap row, store the new image, and
      // repoint the index.  Row count is unchanged (same logical row).
      heap_->erase(*existing);
      std::string bytes = tuple.encode(schema());
      RID rid = heap_->insert(bytes);
      index_->insert(key.encode_key(), rid);
      if (out_rid) *out_rid = rid;
      return {};
    }
  }
  return insert(tuple, out_rid);  // no existing key (or no index) -> plain insert
}

bool Table::delete_by_pk(const Value& key) {
  if (!index_) return false;
  auto rid = index_->search(key.encode_key());
  if (!rid) return false;
  return erase(*rid);
}

}  // namespace axiomdb
