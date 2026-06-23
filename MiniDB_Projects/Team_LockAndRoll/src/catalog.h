#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "common.h"
#include "index.h"
#include "storage.h"
#include "table.h"

namespace minidb {

struct TableInfo {
  int oid = -1;
  std::string name;
  Schema schema;
  int pk_index = 0;
  file_id_t file_id = -1;
  std::unique_ptr<TableHeap> heap;
  std::unique_ptr<BPlusTree> index;
  mutable std::shared_mutex mu;  // guards heap+index: shared for reads, unique for writes

  int64_t pk_of(const Tuple& t) const {
    const Value& v = t.value(pk_index);
    if (!v.is_int()) throw DBException("primary key must be a non-null INTEGER");
    return v.as_int();
  }
};

class Catalog {
 public:
  Catalog(DiskManager* dm, BufferPool* bpool, LogManager* log, std::string dir)
      : dm_(dm), bpool_(bpool), log_(log), dir_(std::move(dir)) {}

  TableInfo* create_table(const std::string& name, const Schema& schema, int pk_index);

  TableInfo* get_table(const std::string& name) const;
  std::vector<TableInfo*> tables() const;
  bool exists(const std::string& name) const { return tables_by_name_.count(name) > 0; }

  // opens metadata and heap files but doesn't rebuild indexes
  void load();

 private:
  void persist() const;
  TableInfo* install(int oid, const std::string& name, const Schema& schema, int pk_index);

  DiskManager* dm_;
  BufferPool* bpool_;
  LogManager* log_;
  std::string dir_;
  int next_oid_ = 1;
  std::vector<std::unique_ptr<TableInfo>> tables_;
  std::map<std::string, TableInfo*> tables_by_name_;
};

}  // namespace minidb
