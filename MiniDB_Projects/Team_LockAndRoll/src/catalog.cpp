#include "catalog.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>

namespace minidb {

TableInfo* Catalog::install(int oid, const std::string& name, const Schema& schema,
                            int pk_index) {
  auto info = std::make_unique<TableInfo>();
  info->oid = oid;
  info->name = name;
  info->schema = schema;
  info->pk_index = pk_index;
  info->file_id = dm_->open_file(name + ".tbl");
  info->heap = std::make_unique<TableHeap>(bpool_, dm_, log_, info->file_id, schema);
  info->index = std::make_unique<BPlusTree>();
  TableInfo* raw = info.get();
  tables_by_name_[name] = raw;
  tables_.push_back(std::move(info));
  if (oid >= next_oid_) next_oid_ = oid + 1;
  return raw;
}

TableInfo* Catalog::create_table(const std::string& name, const Schema& schema, int pk_index) {
  if (tables_by_name_.count(name)) throw DBException("table already exists: " + name);
  if (pk_index < 0 || pk_index >= static_cast<int>(schema.size()) ||
      schema.column(pk_index).type != TypeId::INTEGER)
    throw DBException("primary key must be an INTEGER column");
  TableInfo* t = install(next_oid_, name, schema, pk_index);
  persist();
  return t;
}

TableInfo* Catalog::get_table(const std::string& name) const {
  auto it = tables_by_name_.find(name);
  return it == tables_by_name_.end() ? nullptr : it->second;
}

std::vector<TableInfo*> Catalog::tables() const {
  std::vector<TableInfo*> out;
  for (auto& t : tables_) out.push_back(t.get());
  return out;
}

void Catalog::persist() const {
  // write to a temp file, fsync, then atomic rename so a crash leaves either the
  // old catalog or the complete new one, never a half-written file
  const std::string final_path = dir_ + "/catalog.meta";
  const std::string tmp_path = final_path + ".tmp";
  {
    std::ofstream f(tmp_path, std::ios::trunc);
    if (!f) throw DBException("cannot write catalog");
    f << tables_.size() << "\n";
    for (auto& t : tables_) {
      f << t->oid << " " << t->pk_index << " " << t->schema.size() << " " << t->name << "\n";
      for (const Column& c : t->schema.columns())
        f << c.name << " " << static_cast<int>(c.type) << "\n";
    }
    f.flush();
    if (!f) throw DBException("cannot write catalog");
  }
  int fd = ::open(tmp_path.c_str(), O_RDONLY);
  if (fd >= 0) {
    ::fsync(fd);
    ::close(fd);
  }
  if (std::rename(tmp_path.c_str(), final_path.c_str()) != 0)
    throw DBException("cannot commit catalog");
}

void Catalog::load() {
  std::ifstream f(dir_ + "/catalog.meta");
  if (!f.good()) return;  // fresh database
  size_t ntables = 0;
  f >> ntables;
  for (size_t i = 0; i < ntables; i++) {
    int oid, pk_index;
    size_t ncols;
    std::string name;
    f >> oid >> pk_index >> ncols >> name;
    std::vector<Column> cols;
    for (size_t c = 0; c < ncols; c++) {
      std::string cname;
      int ctype;
      f >> cname >> ctype;
      cols.push_back({cname, static_cast<TypeId>(ctype)});
    }
    install(oid, name, Schema(std::move(cols)), pk_index);
  }
}

}  // namespace minidb
