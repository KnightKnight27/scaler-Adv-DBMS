#include "catalog/catalog.h"

#include "buffer/buffer_pool.h"
#include "index/b_plus_tree.h"
#include "storage/disk_manager.h"

#include <cstdio>

namespace minidb {

using namespace std;

CatalogManager::CatalogManager(DiskManager* dm) : dm_(dm) {}

CatalogManager::~CatalogManager() {
  tables_.clear();
  indexes_.clear();
  tableBps_.clear();
  tableDms_.clear();
}

string CatalogManager::MakeHeapPath(const string& tableName) const {
  return string("/tmp/minidb_") + tableName + ".heap";
}

bool CatalogManager::CreateTable(const string& tableName, const Schema& schema) {
  (void)dm_;
  if (HasTable(tableName))
    return false;
  string path = MakeHeapPath(tableName);
  remove(path.c_str());
  tableDms_[tableName] = make_unique<DiskManager>(path);
  auto dm = tableDms_[tableName].get();
  tableBps_[tableName] = make_unique<BufferPool>(dm);
  auto bp = tableBps_[tableName].get();
  tables_[tableName] = make_unique<TableHeap>(dm, schema, bp);
  schemas_[tableName] = schema;

  for (const auto& col : schema.GetColumns()) {
    if (col.IsPrimaryKey()) {
      CreateIndex(tableName, col.GetName());
    }
  }
  return true;
}

bool CatalogManager::DropTable(const string& tableName) {
  auto it = tables_.find(tableName);
  if (it == tables_.end())
    return false;
  tables_.erase(it);
  schemas_.erase(tableName);
  tableDms_.erase(tableName);
  tableBps_.erase(tableName);
  auto idxIt = indexes_.find(tableName);
  if (idxIt != indexes_.end()) {
    for (const auto& [col, index] : idxIt->second) {
      string path = MakeHeapPath(tableName) + "_" + col + ".idx";
      remove(path.c_str());
      string meta = path + ".meta";
      remove(meta.c_str());
    }
    indexes_.erase(idxIt);
  }
  string path = MakeHeapPath(tableName);
  remove(path.c_str());
  return true;
}

bool CatalogManager::CreateIndex(const string& tableName, const string& columnName) {
  if (!HasTable(tableName))
    return false;
  string path = MakeHeapPath(tableName) + "_" + columnName + ".idx";
  remove(path.c_str());
  string meta = path + ".meta";
  remove(meta.c_str());
  indexes_[tableName][columnName] = make_unique<BPlusTree>(path);
  return true;
}

BPlusTree* CatalogManager::GetIndex(const string& tableName, const string& columnName) const {
  auto it = indexes_.find(tableName);
  if (it == indexes_.end())
    return nullptr;
  auto cit = it->second.find(columnName);
  if (cit == it->second.end())
    return nullptr;
  return cit->second.get();
}

bool CatalogManager::HasTable(const string& tableName) const {
  return tables_.find(tableName) != tables_.end();
}

TableHeap* CatalogManager::GetTable(const string& tableName) const {
  auto it = tables_.find(tableName);
  if (it == tables_.end())
    return nullptr;
  return it->second.get();
}

const Schema* CatalogManager::GetSchema(const string& tableName) const {
  auto it = schemas_.find(tableName);
  if (it == schemas_.end())
    return nullptr;
  return &it->second;
}

vector<string> CatalogManager::ListTables() const {
  vector<string> result;
  result.reserve(tables_.size());
  for (const auto& p : tables_) {
    result.push_back(p.first);
  }
  return result;
}

} // namespace minidb