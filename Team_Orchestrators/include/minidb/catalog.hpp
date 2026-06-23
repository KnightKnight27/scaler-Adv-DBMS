#pragma once
// System catalog: table metadata (schema + heap page directory), persisted to a
// small text file alongside the data file.
#include "minidb/schema.hpp"
#include "minidb/storage/page.hpp"
#include "minidb/storage/storage_engine.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

using IndexId = uint32_t;

struct TableMeta {
  TableId id = 0;
  std::string name;
  Schema schema;
  std::vector<PageId> data_pages;  // heap pages belonging to this table
};

struct IndexMeta {
  IndexId id = 0;
  std::string name;
  TableId table = 0;
  size_t column = 0;  // indexed column position within the table schema
};

// Optimizer statistics, populated by ANALYZE (distinct) and maintained
// incrementally (row_count). Held in memory only; rerun ANALYZE after reopen
// for accurate selectivity.
struct TableStats {
  size_t row_count = 0;
  std::unordered_map<size_t, size_t> distinct;  // column index -> distinct count
};

class Catalog {
 public:
  // Returns the new table's id; throws if the name already exists.
  TableId create_table(const std::string& name, const Schema& schema);

  bool exists(const std::string& name) const;
  TableMeta& by_name(const std::string& name);
  TableMeta& by_id(TableId id);
  const TableMeta& by_id(TableId id) const;
  std::vector<std::string> table_names() const;

  // Index metadata. Throws if the index name already exists.
  IndexId create_index(const std::string& name, TableId table, size_t column);
  bool index_exists(const std::string& name) const;
  std::vector<const IndexMeta*> indexes_for(TableId table) const;
  const std::vector<IndexMeta>& indexes() const { return indexes_; }

  // Mutable per-table statistics (created on first access).
  TableStats& stats(TableId table) { return stats_[table]; }

  void save(const std::string& path) const;
  void load(const std::string& path);

 private:
  std::vector<TableMeta> tables_;
  std::unordered_map<std::string, size_t> by_name_;
  std::unordered_map<TableId, size_t> by_id_;
  TableId next_id_ = 1;

  std::vector<IndexMeta> indexes_;
  std::unordered_map<std::string, size_t> index_by_name_;
  IndexId next_index_id_ = 1;

  std::unordered_map<TableId, TableStats> stats_;
};

}  // namespace minidb
