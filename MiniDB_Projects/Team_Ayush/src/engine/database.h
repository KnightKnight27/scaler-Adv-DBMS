#pragma once
#include <memory>
#include <string>
#include "catalog/catalog.h"
#include "common/status.h"
#include "sql/ast.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

namespace minidb {

// Database ties the storage stack (disk manager + buffer pool) to the system
// catalog. Page 0 of the file is reserved for the serialized catalog. The class
// owns table creation and metadata persistence; query execution lives in the
// executor, which operates on a Database.
class Database {
 public:
  explicit Database(const std::string& path);
  ~Database();

  BufferPool* pool() { return pool_.get(); }
  Catalog&    catalog() { return catalog_; }

  TableInfo* GetTable(const std::string& name) { return catalog_.Find(name); }

  // Create heap + (if a PK is declared) a B+Tree index, register in catalog.
  Status CreateTable(const CreateTableStmt& stmt);

  // Persist the catalog (page 0) and flush all dirty pages.
  void Flush();
  void SaveCatalog();

 private:
  std::unique_ptr<DiskManager> dm_;
  std::unique_ptr<BufferPool>  pool_;
  Catalog                      catalog_;
};

}  // namespace minidb
