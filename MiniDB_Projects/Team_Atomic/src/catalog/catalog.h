#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/types.h"
#include "record/schema.h"
#include "storage/buffer_pool_manager.h"
#include "storage/table_heap.h"
#include "storage/row_store.h"
#include "index/bplus_tree.h"

namespace minidb {

// Per-table metadata. The page ids let us reopen the heap and index on restart.
struct TableMeta {
  std::string name;
  Schema schema;
  EngineType engine = EngineType::Heap;
  page_id_t heap_first_page = INVALID_PAGE_ID;
  page_id_t index_header_page = INVALID_PAGE_ID;  // PK index; -1 if none
  std::shared_ptr<RowStore> store;

  bool HasKeyAccess() const { return store && store->SupportsKeyAccess(); }
};

// Maps table names to their storage, persisted to a `<db>.catalog` sidecar so
// tables reopen across restarts.
class Catalog {
 public:
  Catalog(BufferPoolManager* bpm, std::string catalog_file, std::string lsm_prefix);

  // Create a table backed by `engine`; builds the appropriate row store.
  TableMeta* CreateTable(const std::string& name, const Schema& schema,
                         EngineType engine = EngineType::Heap);

  TableMeta* GetTable(const std::string& name);
  std::vector<std::string> TableNames() const;

  void Save();
  void Load();

  // Flush every table's in-memory store state (LSM memtables) at a checkpoint.
  void SyncStores();

 private:
  std::string LsmPrefixFor(const std::string& table) const;

  BufferPoolManager* bpm_;
  std::string catalog_file_;
  std::string lsm_prefix_;
  std::unordered_map<std::string, TableMeta> tables_;
};

}  // namespace minidb
