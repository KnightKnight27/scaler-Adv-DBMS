#include "catalog/catalog.h"

namespace minidb {

TableInfo& Catalog::add_table(std::string name, Schema schema, int pk_col,
                              std::unique_ptr<StorageEngine> storage) {
  std::string key = name;
  TableInfo info{std::move(name), std::move(schema), pk_col, std::move(storage)};
  auto [it, _] = tables_.insert_or_assign(std::move(key), std::move(info));
  return it->second;
}

TableInfo* Catalog::get(const std::string& name) {
  auto it = tables_.find(name);
  return it == tables_.end() ? nullptr : &it->second;
}

Schema qualified_schema(const TableInfo& table) {
  Schema s;
  for (const Column& c : table.schema.columns)
    s.columns.push_back({table.name + "." + c.name, c.type});
  return s;
}

}  // namespace minidb
