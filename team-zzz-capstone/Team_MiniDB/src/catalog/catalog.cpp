#include "catalog.h"

#include <fstream>
#include <sstream>

namespace minidb {

Catalog::Catalog(BufferPool *bpm, std::string catalog_path) : bpm_(bpm), catalog_path_(std::move(catalog_path)) {
  Load();
}

TableInfo *Catalog::CreateTable(const std::string &name, const std::vector<Column> &columns) {
  if (tables_.count(name)) return nullptr;

  auto info = std::make_unique<TableInfo>();
  info->name = name;
  info->schema = Schema(columns);

  page_id_t heap_pid;
  Page *p = bpm_->NewPage(&heap_pid);
  HeapPage hp(p->GetData());
  hp.Init();
  bpm_->UnpinPage(heap_pid, true);
  info->heap_first_page = heap_pid;
  info->heap = std::make_unique<HeapFile>(bpm_, heap_pid);

  if (info->schema.PrimaryKeyIndex() >= 0) {
    info->btree_root = BPlusTree::CreateEmpty(bpm_);
    info->index = std::make_unique<BPlusTree>(bpm_, info->btree_root);
  } else {
    info->btree_root = INVALID_PAGE_ID;
  }

  TableInfo *raw = info.get();
  tables_[name] = std::move(info);
  Save();
  return raw;
}

TableInfo *Catalog::GetTable(const std::string &name) {
  auto it = tables_.find(name);
  return it == tables_.end() ? nullptr : it->second.get();
}

bool Catalog::TableExists(const std::string &name) const { return tables_.count(name) > 0; }

std::vector<std::string> Catalog::TableNames() const {
  std::vector<std::string> names;
  for (auto &[name, _] : tables_) names.push_back(name);
  return names;
}

void Catalog::Save() {
  std::ofstream out(catalog_path_, std::ios::trunc);
  for (auto &[name, info] : tables_) {
    out << "TABLE " << name << " " << info->schema.NumColumns() << " "
        << info->heap_first_page << " " << info->btree_root << " " << info->num_rows << "\n";
    for (auto &col : info->schema.Columns()) {
      out << "COL " << col.name << " " << (col.type == TypeId::INTEGER ? "INT" : "VARCHAR") << " "
          << (col.is_primary_key ? "PK" : "NOPK") << "\n";
    }
  }
}

void Catalog::Load() {
  std::ifstream in(catalog_path_);
  if (!in.is_open()) return;

  std::string line;
  TableInfo *current = nullptr;
  std::vector<Column> pending_cols;
  std::string pending_name;
  page_id_t pending_heap = INVALID_PAGE_ID, pending_root = INVALID_PAGE_ID;
  int64_t pending_rows = 0;

  auto flush = [&]() {
    if (pending_name.empty()) return;
    auto info = std::make_unique<TableInfo>();
    info->name = pending_name;
    info->schema = Schema(pending_cols);
    info->heap_first_page = pending_heap;
    info->btree_root = pending_root;
    info->num_rows = pending_rows;
    info->heap = std::make_unique<HeapFile>(bpm_, pending_heap);
    if (pending_root != INVALID_PAGE_ID) info->index = std::make_unique<BPlusTree>(bpm_, pending_root);
    tables_[pending_name] = std::move(info);
    pending_cols.clear();
  };

  while (std::getline(in, line)) {
    std::istringstream iss(line);
    std::string tag;
    iss >> tag;
    if (tag == "TABLE") {
      flush();
      int num_cols;
      iss >> pending_name >> num_cols >> pending_heap >> pending_root >> pending_rows;
      (void)num_cols;
    } else if (tag == "COL") {
      std::string colname, typestr, pkstr;
      iss >> colname >> typestr >> pkstr;
      Column c;
      c.name = colname;
      c.type = typestr == "INT" ? TypeId::INTEGER : TypeId::VARCHAR;
      c.is_primary_key = (pkstr == "PK");
      pending_cols.push_back(c);
    }
  }
  flush();
}

}  // namespace minidb
