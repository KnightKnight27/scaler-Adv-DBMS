#include "minidb/catalog.hpp"

#include <fstream>
#include <stdexcept>

namespace minidb {

TableId Catalog::create_table(const std::string& name, const Schema& schema) {
  if (exists(name)) throw std::runtime_error("table already exists: " + name);
  TableMeta m;
  m.id = next_id_++;
  m.name = name;
  m.schema = schema;
  size_t idx = tables_.size();
  tables_.push_back(std::move(m));
  by_name_[name] = idx;
  by_id_[tables_[idx].id] = idx;
  return tables_[idx].id;
}

bool Catalog::exists(const std::string& name) const {
  return by_name_.find(name) != by_name_.end();
}

TableMeta& Catalog::by_name(const std::string& name) {
  auto it = by_name_.find(name);
  if (it == by_name_.end()) throw std::runtime_error("no such table: " + name);
  return tables_[it->second];
}

TableMeta& Catalog::by_id(TableId id) {
  auto it = by_id_.find(id);
  if (it == by_id_.end()) throw std::runtime_error("no such table id");
  return tables_[it->second];
}

const TableMeta& Catalog::by_id(TableId id) const {
  auto it = by_id_.find(id);
  if (it == by_id_.end()) throw std::runtime_error("no such table id");
  return tables_[it->second];
}

IndexId Catalog::create_index(const std::string& name, TableId table, size_t column) {
  if (index_exists(name)) throw std::runtime_error("index already exists: " + name);
  IndexMeta m;
  m.id = next_index_id_++;
  m.name = name;
  m.table = table;
  m.column = column;
  size_t idx = indexes_.size();
  indexes_.push_back(std::move(m));
  index_by_name_[name] = idx;
  return indexes_[idx].id;
}

bool Catalog::index_exists(const std::string& name) const {
  return index_by_name_.find(name) != index_by_name_.end();
}

std::vector<const IndexMeta*> Catalog::indexes_for(TableId table) const {
  std::vector<const IndexMeta*> out;
  for (const auto& ix : indexes_)
    if (ix.table == table) out.push_back(&ix);
  return out;
}

std::vector<std::string> Catalog::table_names() const {
  std::vector<std::string> out;
  out.reserve(tables_.size());
  for (const auto& t : tables_) out.push_back(t.name);
  return out;
}

void Catalog::save(const std::string& path) const {
  std::ofstream f(path, std::ios::trunc);
  if (!f) throw std::runtime_error("catalog: cannot write " + path);
  f << "MINIDB_CATALOG 1\n";
  f << "NEXT_ID " << next_id_ << "\n";
  f << "TABLES " << tables_.size() << "\n";
  for (const auto& t : tables_) {
    f << "TABLE " << t.id << " " << t.name << " " << t.schema.size() << "\n";
    for (const auto& c : t.schema.columns()) {
      f << "COL " << c.name << " " << static_cast<int>(c.type) << " "
        << (c.primary_key ? 1 : 0) << "\n";
    }
    f << "PAGES " << t.data_pages.size();
    for (PageId p : t.data_pages) f << " " << p;
    f << "\n";
  }
  f << "NEXT_INDEX_ID " << next_index_id_ << "\n";
  f << "INDEXES " << indexes_.size() << "\n";
  for (const auto& ix : indexes_) {
    f << "INDEX " << ix.id << " " << ix.name << " " << ix.table << " "
      << ix.column << "\n";
  }
}

void Catalog::load(const std::string& path) {
  std::ifstream f(path);
  if (!f) return;  // fresh database
  tables_.clear();
  by_name_.clear();
  by_id_.clear();

  std::string tok;
  f >> tok;  // MINIDB_CATALOG
  int version = 0;
  f >> version;
  f >> tok >> next_id_;        // NEXT_ID <n>
  size_t ntables = 0;
  f >> tok >> ntables;         // TABLES <n>

  for (size_t i = 0; i < ntables; ++i) {
    TableMeta m;
    size_t ncols = 0;
    f >> tok >> m.id >> m.name >> ncols;  // TABLE id name ncols
    std::vector<Column> cols;
    for (size_t c = 0; c < ncols; ++c) {
      Column col;
      int type = 0, pk = 0;
      f >> tok >> col.name >> type >> pk;  // COL name type pk
      col.type = static_cast<TypeId>(type);
      col.primary_key = (pk != 0);
      cols.push_back(col);
    }
    m.schema = Schema(std::move(cols));
    size_t npages = 0;
    f >> tok >> npages;  // PAGES <n> ...
    for (size_t p = 0; p < npages; ++p) {
      PageId pid = 0;
      f >> pid;
      m.data_pages.push_back(pid);
    }
    size_t idx = tables_.size();
    tables_.push_back(std::move(m));
    by_name_[tables_[idx].name] = idx;
    by_id_[tables_[idx].id] = idx;
  }

  // Optional index section (absent in older catalogs).
  if (f >> tok && tok == "NEXT_INDEX_ID") {
    f >> next_index_id_;
    size_t nindexes = 0;
    f >> tok >> nindexes;  // INDEXES <n>
    for (size_t i = 0; i < nindexes; ++i) {
      IndexMeta ix;
      f >> tok >> ix.id >> ix.name >> ix.table >> ix.column;  // INDEX id name table col
      size_t at = indexes_.size();
      indexes_.push_back(ix);
      index_by_name_[ix.name] = at;
    }
  }
}

}  // namespace minidb
