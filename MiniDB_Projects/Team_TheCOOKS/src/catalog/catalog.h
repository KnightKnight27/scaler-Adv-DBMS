#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "buffer/buffer_pool.h"
#include "catalog/schema.h"
#include "common/config.h"
#include "common/status.h"

namespace walterdb {

class Table;  // runtime accessor, defined in catalog/table.h

// ---------------------------------------------------------------------------
// TableInfo -- the persisted metadata describing one table:
//   * its schema,
//   * the head page of its heap file (where rows live), and
//   * the meta page of its primary-key B+tree index (INVALID if no PK),
//   * lightweight statistics (row count) the optimizer reads for selectivity.
// ---------------------------------------------------------------------------
struct TableInfo {
  uint32_t table_id = 0;
  std::string name;
  Schema schema;
  page_id_t heap_first_page = INVALID_PAGE_ID;
  page_id_t index_meta_page = INVALID_PAGE_ID;  // primary-key index
  int pk_column = -1;                            // index into schema, -1 if none
  uint64_t row_count = 0;

  bool has_primary_index() const { return index_meta_page != INVALID_PAGE_ID; }
};

// ---------------------------------------------------------------------------
// Catalog -- the system's metadata store and table factory.
//
// It maps table names to TableInfo, creates the backing heap + index when a
// table is declared, and persists all of this to a small sidecar file so tables
// survive a restart.  DDL is intentionally NOT transactional / WAL-logged (a
// stated simplification): the catalog is rewritten and flushed immediately on
// CREATE/DROP, and the crash-recovery story concerns row-level INSERT/DELETE on
// existing tables, not schema changes.
//
// The catalog also caches one open Table accessor per table so the heap tail
// pointer and index handle are not rebuilt on every statement.
// ---------------------------------------------------------------------------
class Catalog {
 public:
  // Loads existing metadata from `catalog_path` if it exists.
  Catalog(BufferPool* bpm, std::string catalog_path);
  ~Catalog();

  // Create a table; allocates its heap (and a PK index if the schema declares a
  // primary key).  Fails if the name already exists.
  Status create_table(const std::string& name, const Schema& schema, TableInfo** out = nullptr);

  // Metadata lookup (case-insensitive); nullptr if absent.
  TableInfo* get_table(const std::string& name);

  // Runtime accessor (heap + index) for a table, lazily opened and cached.
  // nullptr if the table does not exist.
  Table* open_table(const std::string& name);

  // Same, by numeric table id (used by crash recovery to resolve log records).
  Table* open_table_by_id(uint32_t table_id);

  std::vector<std::string> table_names() const;

  // Persist the current metadata to disk (called after DDL and on close).
  void save() const;

  BufferPool* buffer_pool() { return bpm_; }

 private:
  void load();
  std::string lower(const std::string& s) const;

  BufferPool* bpm_;
  std::string path_;
  uint32_t next_table_id_ = 1;
  std::unordered_map<std::string, std::unique_ptr<TableInfo>> tables_;  // key: lowercased name
  std::unordered_map<std::string, std::unique_ptr<Table>> open_;        // key: lowercased name
  std::mutex open_latch_;  // guards the open-table cache for concurrent txns
};

}  // namespace walterdb
