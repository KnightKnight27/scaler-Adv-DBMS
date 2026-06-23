#include "catalog/catalog.h"
#include <fstream>
#include <sstream>

namespace minidb {

Catalog::Catalog(std::string catalog_file) : file_(std::move(catalog_file)) {
  Load();
}

TableInfo *Catalog::GetTable(const std::string &name) {
  auto it = tables_.find(name);
  return it == tables_.end() ? nullptr : it->second.get();
}

std::vector<std::string> Catalog::TableNames() const {
  std::vector<std::string> names;
  for (auto &kv : tables_) names.push_back(kv.first);
  return names;
}

TableInfo *Catalog::CreateTable(const TableInfo &info) {
  auto ptr = std::make_unique<TableInfo>(info);
  TableInfo *raw = ptr.get();
  tables_[info.name] = std::move(ptr);
  Save();
  return raw;
}

// Catalog file format (one token stream, space/newline separated):
//   <num_tables>
//   for each table:
//     <name> <storage:0|1> <heap_first> <index_root> <lsm_dir|-> <num_cols>
//     for each col: <name> <type:0|1> <is_pk:0|1>
void Catalog::Save() {
  std::ofstream out(file_, std::ios::trunc);
  out << tables_.size() << "\n";
  for (auto &kv : tables_) {
    const TableInfo &t = *kv.second;
    out << t.name << " " << static_cast<int>(t.storage) << " " << t.heap_first_page
        << " " << t.index_root_page << " " << (t.lsm_dir.empty() ? "-" : t.lsm_dir)
        << " " << t.schema.num_columns() << "\n";
    for (size_t i = 0; i < t.schema.num_columns(); ++i) {
      const Column &c = t.schema.column(i);
      out << c.name << " " << static_cast<int>(c.type) << " "
          << (c.is_primary_key ? 1 : 0) << "\n";
    }
  }
}

void Catalog::Load() {
  std::ifstream in(file_);
  if (!in.is_open()) return;  // no catalog yet
  size_t n = 0;
  if (!(in >> n)) return;
  for (size_t i = 0; i < n; ++i) {
    TableInfo t;
    int storage;
    std::string lsm;
    size_t ncols;
    in >> t.name >> storage >> t.heap_first_page >> t.index_root_page >> lsm >> ncols;
    t.storage = static_cast<StorageType>(storage);
    t.lsm_dir = (lsm == "-") ? "" : lsm;
    std::vector<Column> cols;
    for (size_t c = 0; c < ncols; ++c) {
      Column col;
      int type, pk;
      in >> col.name >> type >> pk;
      col.type = static_cast<TypeId>(type);
      col.is_primary_key = (pk == 1);
      cols.push_back(col);
    }
    t.schema = Schema(std::move(cols));
    tables_[t.name] = std::make_unique<TableInfo>(std::move(t));
  }
}

}  // namespace minidb
