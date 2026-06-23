#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include "common/types.h"
#include "storage/storage_engine.h"

namespace minidb {

// Everything the rest of the system needs to know about one table: its name,
// column schema, which column is the primary key, and the storage engine that
// holds its rows.
struct TableInfo {
  std::string                    name;
  Schema                         schema;
  int                            pk_col = 0;
  std::unique_ptr<StorageEngine> storage;
};

// In-memory registry of tables. MiniDB has no CREATE TABLE statement; tables
// are registered here at startup (see src/main.cpp).
class Catalog {
 public:
  TableInfo& add_table(std::string name, Schema schema, int pk_col,
                       std::unique_ptr<StorageEngine> storage);
  TableInfo* get(const std::string& name);  // nullptr if unknown

 private:
  std::unordered_map<std::string, TableInfo> tables_;
};

// Returns the table's schema with every column name prefixed by the table name
// (e.g. "students.id"). Used by the planner so columns resolve unambiguously,
// including across a join. Types are unchanged, so it also drives row decoding.
Schema qualified_schema(const TableInfo& table);

}  // namespace minidb
