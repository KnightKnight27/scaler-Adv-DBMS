#pragma once

#include "catalog/table_heap.h"
#include "common/types.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace minidb {

using namespace std;

class BufferPool;
class BPlusTree;

class CatalogManager {
public:
  CatalogManager(DiskManager* dm);
  ~CatalogManager();

  bool CreateTable(const string& tableName, const Schema& schema);
  bool DropTable(const string& tableName);

  bool HasTable(const string& tableName) const;
  TableHeap* GetTable(const string& tableName) const;
  const Schema* GetSchema(const string& tableName) const;

  bool CreateIndex(const string& tableName, const string& columnName);
  BPlusTree* GetIndex(const string& tableName, const string& columnName) const;

  vector<string> ListTables() const;

private:
  string MakeHeapPath(const string& tableName) const;

  DiskManager* dm_;
  unordered_map<string, unique_ptr<TableHeap>> tables_;
  unordered_map<string, Schema> schemas_;
  unordered_map<string, unique_ptr<DiskManager>> tableDms_;
  unordered_map<string, unique_ptr<BufferPool>> tableBps_;
  unordered_map<string, unordered_map<string, unique_ptr<BPlusTree>>> indexes_;
};

} // namespace minidb