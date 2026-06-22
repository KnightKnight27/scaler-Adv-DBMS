// ============================================================================
// catalog.h  --  The system catalog: MiniDB's knowledge of its own schema.
//
// The catalog answers "what tables exist, what columns do they have, where does
// each table's data start, and which columns are indexed?".  Every higher layer
// (parser/binder, optimizer, executor) consults it.
//
// What is persisted vs rebuilt:
//   * PERSISTED to a small text file: table names, their schemas, the first
//     page id of each heap, the primary-key column, and which columns have an
//     index.  This is the minimum needed to find the data again after restart.
//   * REBUILT in memory on startup: the actual B+ Tree index structures, by
//     scanning each heap.  This is why the index needs no on-disk format.
// ============================================================================
#pragma once

#include "common/common.h"
#include "index/bplus_tree.h"
#include "record/schema.h"
#include "storage/table_heap.h"

namespace minidb {

// An index on one column of a table.
struct IndexInfo {
  string                name;
  int                        col_idx;   // which column this index keys on
  unique_ptr<BPlusTree> tree;
};

// Everything we know about one table.
struct TableInfo {
  string                            name;
  Schema                                 schema;
  page_id_t                              first_page_id;
  int                                    pk_col{-1};   // -1 if no primary key
  unique_ptr<TableHeap>             heap;
  vector<unique_ptr<IndexInfo>> indexes;     // [0] is the PK index
  int                                    num_tuples{0}; // simple stat for the optimizer

  // Find an index keyed on `col_idx`, or nullptr if none exists.
  IndexInfo *indexOnColumn(int col_idx) const {
    for (auto &idx : indexes)
      if (idx->col_idx == col_idx) return idx.get();
    return nullptr;
  }
};

class Catalog {
 public:
  Catalog(BufferPool *bpm, string catalog_file);

  // DDL.  createTable/createIndex also persist the updated metadata.
  TableInfo *createTable(const string &name, const Schema &schema, int pk_col);
  IndexInfo *createIndex(const string &index_name, const string &table,
                         const string &column);

  TableInfo *getTable(const string &name);  // nullptr if absent
  vector<string> tableNames() const;

  // Startup sequence helpers.
  void load();               // read metadata file, open heaps (no index build)
  void rebuildAllIndexes();  // scan heaps to (re)populate every index + counts

 private:
  void persist();            // write the metadata file
  void buildIndex(TableInfo *t, IndexInfo *idx);  // populate one index from heap

  BufferPool *bpm_;
  string catalog_file_;
  unordered_map<string, unique_ptr<TableInfo>> tables_;
};

}  // namespace minidb
