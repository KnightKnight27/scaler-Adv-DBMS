#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../index/b_plus_tree.h"
#include "../storage/buffer_pool.h"
#include "../storage/heap_file.h"
#include "schema.h"

namespace minidb {

// Per-table metadata plus the live storage handles built on top of the
// shared buffer pool. `num_rows` is the only statistic the optimizer needs
// for the simple selectivity model used here.
struct TableInfo {
  std::string name;
  Schema schema;
  page_id_t heap_first_page;
  page_id_t btree_root;  // INVALID_PAGE_ID if no primary key index
  int64_t num_rows = 0;

  std::unique_ptr<HeapFile> heap;
  std::unique_ptr<BPlusTree> index;
};

// Holds all table definitions for the database. Metadata (schema + root
// page ids) is persisted to a small sidecar "<db>.catalog" text file so
// table structure survives a restart, independent of the page-level WAL
// which only covers data, not DDL.
class Catalog {
 public:
  Catalog(BufferPool *bpm, std::string catalog_path);

  // Creates a new table; `pk_column` may be empty for no primary index.
  TableInfo *CreateTable(const std::string &name, const std::vector<Column> &columns);

  TableInfo *GetTable(const std::string &name);
  bool TableExists(const std::string &name) const;
  std::vector<std::string> TableNames() const;

  void Save();
  void Load();

  // Stats are saved immediately so a restart sees an accurate row count for
  // the optimizer's selectivity estimates; the catalog file is tiny so this
  // is cheap relative to a tuple insert/delete.
  void NotifyRowInserted(const std::string &table) {
    if (auto *t = GetTable(table)) {
      t->num_rows++;
      Save();
    }
  }
  void NotifyRowDeleted(const std::string &table) {
    if (auto *t = GetTable(table)) {
      t->num_rows = std::max<int64_t>(0, t->num_rows - 1);
      Save();
    }
  }

 private:
  BufferPool *bpm_;
  std::string catalog_path_;
  std::unordered_map<std::string, std::unique_ptr<TableInfo>> tables_;
};

}  // namespace minidb
