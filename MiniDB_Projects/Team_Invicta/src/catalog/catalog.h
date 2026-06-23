#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "record/schema.h"

namespace minidb {

// Which storage engine backs a table.
enum class StorageType { HEAP, LSM };

// Persistent metadata for one table.
struct TableInfo {
  std::string name;
  Schema      schema;
  StorageType storage{StorageType::HEAP};
  page_id_t   heap_first_page{INVALID_PAGE_ID};  // heap root (HEAP tables)
  page_id_t   index_root_page{INVALID_PAGE_ID};  // B+ tree root holder (HEAP)
  std::string lsm_dir;                            // SSTable directory (LSM tables)
};

// The Catalog owns table metadata and persists it to a sidecar file so tables
// survive restart (needed for the recovery demo). It is a plain map keyed by
// table name; the engine consults it to plan and execute statements.
class Catalog {
 public:
  explicit Catalog(std::string catalog_file);

  bool HasTable(const std::string &name) const { return tables_.count(name) > 0; }
  TableInfo *GetTable(const std::string &name);
  std::vector<std::string> TableNames() const;

  // Register a new table and persist the catalog. Caller fills in page ids.
  TableInfo *CreateTable(const TableInfo &info);

  // Persist any mutation to a TableInfo (e.g. updated root page id).
  void Save();
  void Load();

 private:
  std::string                                        file_;
  std::unordered_map<std::string, std::unique_ptr<TableInfo>> tables_;
};

}  // namespace minidb
