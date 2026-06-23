#pragma once
// The critical seam: a record-oriented storage interface. Both the heap engine
// (baseline) and the LSM engine (Track C) implement this, so the executor and
// transaction layers depend only on the interface.
#include "minidb/optional.hpp"
#include "minidb/storage/page.hpp"
#include "minidb/types.hpp"
#include <cstdint>
#include <memory>

namespace minidb {

using TableId = uint32_t;

// Physical record id. For the heap engine this is (page, slot); other engines
// may map a logical id onto it.
struct RID {
  PageId page_id = kInvalidPageId;
  uint16_t slot = 0;
  bool operator==(const RID& o) const { return page_id == o.page_id && slot == o.slot; }
};

// Forward-only cursor over a table's live records.
class RecordIterator {
 public:
  virtual ~RecordIterator() = default;
  virtual bool next(RID& rid, Tuple& out) = 0;
};

class StorageEngine {
 public:
  virtual ~StorageEngine() = default;
  virtual RID insert(TableId table, const Tuple& t) = 0;
  virtual bool remove(TableId table, const RID& rid) = 0;
  virtual Optional<Tuple> get(TableId table, const RID& rid) = 0;
  virtual Optional<Tuple> find(TableId table, const Value& key) = 0;
  // RIDs matching an exact key on the given column, via an index if one exists
  // (empty if there is no index on that column or no match).
  virtual std::vector<RID> index_lookup(TableId table, size_t column, const Value& key) = 0;
  virtual std::unique_ptr<RecordIterator> scan(TableId table) = 0;
  virtual void replay_insert(TableId table, const RID& rid, const std::vector<uint8_t>& tuple_bytes) = 0;
  virtual void replay_delete(TableId table, const RID& rid) = 0;
  virtual void flush() = 0;
};

}  // namespace minidb
