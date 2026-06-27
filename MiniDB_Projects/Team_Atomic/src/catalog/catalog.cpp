#include "catalog/catalog.h"
#include <fstream>
#include <sstream>

namespace minidb {

Catalog::Catalog(BufferPoolManager* bpm, std::string catalog_file,
                 std::string lsm_prefix)
    : bpm_(bpm), catalog_file_(std::move(catalog_file)),
      lsm_prefix_(std::move(lsm_prefix)) {
  Load();
}

std::string Catalog::LsmPrefixFor(const std::string& table) const {
  return lsm_prefix_ + "." + table;
}

TableMeta* Catalog::CreateTable(const std::string& name, const Schema& schema,
                                EngineType engine) {
  if (tables_.count(name)) throw DBError("table already exists: " + name);

  bool int_pk = schema.HasPk() &&
                schema.GetColumn(schema.PkIndex()).type == TypeId::INTEGER;
  if (engine == EngineType::LSM && !int_pk)
    throw DBError("LSM tables require an INTEGER PRIMARY KEY");

  TableMeta meta;
  meta.name = name;
  meta.schema = schema;
  meta.engine = engine;

  if (engine == EngineType::LSM) {
    auto lsm = std::make_shared<LSMTree>(LsmPrefixFor(name));
    meta.store = std::make_shared<LsmRowStore>(std::move(lsm));
  } else {
    meta.heap_first_page = TableHeap::Create(bpm_);
    auto heap = std::make_shared<TableHeap>(bpm_, meta.heap_first_page);
    std::shared_ptr<BPlusTree> index;
    if (int_pk) {
      meta.index_header_page = BPlusTree::Create(bpm_);
      index = std::make_shared<BPlusTree>(bpm_, meta.index_header_page);
    }
    meta.store = std::make_shared<HeapRowStore>(std::move(heap), std::move(index));
  }

  tables_[name] = std::move(meta);
  Save();
  return &tables_[name];
}

TableMeta* Catalog::GetTable(const std::string& name) {
  auto it = tables_.find(name);
  return it == tables_.end() ? nullptr : &it->second;
}

std::vector<std::string> Catalog::TableNames() const {
  std::vector<std::string> names;
  for (auto& [n, _] : tables_) names.push_back(n);
  return names;
}

void Catalog::SyncStores() {
  for (auto& [name, m] : tables_) {
    (void)name;
    if (m.store) m.store->Sync();
  }
}

void Catalog::Save() {
  std::ofstream out(catalog_file_, std::ios::trunc);
  out << tables_.size() << "\n";
  for (auto& [name, m] : tables_) {
    out << name << " " << static_cast<int>(m.engine) << " " << m.heap_first_page
        << " " << m.index_header_page << " " << m.schema.PkIndex() << " "
        << m.schema.ColumnCount() << "\n";
    for (auto& col : m.schema.Columns()) {
      out << col.name << " " << static_cast<int>(col.type) << "\n";
    }
  }
}

void Catalog::Load() {
  std::ifstream in(catalog_file_);
  if (!in.is_open()) return;  // fresh database
  size_t n = 0;
  in >> n;
  for (size_t i = 0; i < n; i++) {
    std::string name;
    int engine_i;
    page_id_t heap_pg, idx_pg;
    int pk, ncols;
    in >> name >> engine_i >> heap_pg >> idx_pg >> pk >> ncols;
    std::vector<Column> cols;
    for (int c = 0; c < ncols; c++) {
      std::string cname;
      int ctype;
      in >> cname >> ctype;
      cols.push_back({cname, static_cast<TypeId>(ctype)});
    }
    TableMeta m;
    m.name = name;
    m.schema = Schema(cols, pk);
    m.engine = static_cast<EngineType>(engine_i);
    m.heap_first_page = heap_pg;
    m.index_header_page = idx_pg;
    if (m.engine == EngineType::LSM) {
      auto lsm = std::make_shared<LSMTree>(LsmPrefixFor(name));
      m.store = std::make_shared<LsmRowStore>(std::move(lsm));
    } else {
      auto heap = std::make_shared<TableHeap>(bpm_, heap_pg);
      std::shared_ptr<BPlusTree> index;
      if (idx_pg != INVALID_PAGE_ID)
        index = std::make_shared<BPlusTree>(bpm_, idx_pg);
      m.store = std::make_shared<HeapRowStore>(std::move(heap), std::move(index));
    }
    tables_[name] = std::move(m);
  }
}

}  // namespace minidb
