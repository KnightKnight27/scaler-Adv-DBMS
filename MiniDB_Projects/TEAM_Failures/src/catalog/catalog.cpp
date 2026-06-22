#include "catalog/catalog.h"

#include "record/tuple.h"

namespace minidb {

Catalog::Catalog(BufferPool *bpm, string catalog_file)
    : bpm_(bpm), catalog_file_(move(catalog_file)) {}

TableInfo *Catalog::createTable(const string &name, const Schema &schema,
                                int pk_col) {
  if (tables_.count(name)) throw BinderError("table already exists: " + name);

  auto info = make_unique<TableInfo>();
  info->name = name;
  info->schema = schema;
  info->pk_col = pk_col;
  info->heap = make_unique<TableHeap>(bpm_, INVALID_PAGE_ID);
  info->first_page_id = info->heap->first();

  // A primary key automatically gets an index (so PK lookups use the B+ Tree).
  if (pk_col >= 0) {
    auto idx = make_unique<IndexInfo>();
    idx->name = name + "_pk";
    idx->col_idx = pk_col;
    idx->tree = make_unique<BPlusTree>();
    info->indexes.push_back(move(idx));
  }

  TableInfo *raw = info.get();
  tables_[name] = move(info);
  persist();
  return raw;
}

IndexInfo *Catalog::createIndex(const string &index_name,
                                const string &table,
                                const string &column) {
  TableInfo *t = getTable(table);
  if (t == nullptr) throw BinderError("no such table: " + table);
  int col = t->schema.getColIdx(column);
  if (col < 0) throw BinderError("no such column: " + column);

  auto idx = make_unique<IndexInfo>();
  idx->name = index_name;
  idx->col_idx = col;
  idx->tree = make_unique<BPlusTree>();
  IndexInfo *raw = idx.get();
  t->indexes.push_back(move(idx));
  buildIndex(t, raw);   // populate it from the existing rows
  persist();
  return raw;
}

TableInfo *Catalog::getTable(const string &name) {
  auto it = tables_.find(name);
  return it == tables_.end() ? nullptr : it->second.get();
}

vector<string> Catalog::tableNames() const {
  vector<string> v;
  for (auto &[n, _] : tables_) v.push_back(n);
  return v;
}

void Catalog::buildIndex(TableInfo *t, IndexInfo *idx) {
  for (auto it = t->heap->begin(); it != t->heap->end(); it.advance()) {
    Tuple tup = Tuple::deserialize(it.bytes().data(), t->schema);
    idx->tree->insert(tup.value(idx->col_idx), it.rid());
  }
}

void Catalog::rebuildAllIndexes() {
  // One pass per table: count rows and (re)populate every index.
  for (auto &[name, t] : tables_) {
    for (auto &idx : t->indexes) idx->tree = make_unique<BPlusTree>();
    int count = 0;
    for (auto it = t->heap->begin(); it != t->heap->end(); it.advance()) {
      Tuple tup = Tuple::deserialize(it.bytes().data(), t->schema);
      for (auto &idx : t->indexes) idx->tree->insert(tup.value(idx->col_idx), it.rid());
      ++count;
    }
    t->num_tuples = count;
  }
}

// ----------------------------- persistence ---------------------------------
// Text format, one table block per table:
//   TABLE <name> <first_page_id> <pk_col> <num_cols>
//   COL <colname> <type 0=INT 1=VARCHAR>      (num_cols of these)
//   INDEX <index_name> <col_idx>              (zero or more)
//   END
void Catalog::persist() {
  ofstream out(catalog_file_, ios::trunc);
  for (auto &[name, t] : tables_) {
    out << "TABLE " << t->name << " " << t->first_page_id << " " << t->pk_col
        << " " << t->schema.size() << "\n";
    for (auto &c : t->schema.columns())
      out << "COL " << c.name << " " << static_cast<int>(c.type) << "\n";
    for (auto &idx : t->indexes)
      out << "INDEX " << idx->name << " " << idx->col_idx << "\n";
    out << "END\n";
  }
}

void Catalog::load() {
  ifstream in(catalog_file_);
  if (!in.is_open()) return;   // fresh database: nothing to load

  string tok;
  while (in >> tok) {
    if (tok != "TABLE") continue;
    auto info = make_unique<TableInfo>();
    int num_cols, type_pk;
    in >> info->name >> info->first_page_id >> type_pk >> num_cols;
    info->pk_col = type_pk;

    vector<Column> cols;
    string line;
    for (int i = 0; i < num_cols; ++i) {
      string kw, cname; int ctype;
      in >> kw >> cname >> ctype;
      cols.push_back({cname, static_cast<TypeId>(ctype)});
    }
    info->schema = Schema(move(cols));
    // Re-open the heap at its persisted first page.
    info->heap = make_unique<TableHeap>(bpm_, info->first_page_id);

    // Read INDEX lines until END.
    string kw;
    while (in >> kw && kw != "END") {
      if (kw == "INDEX") {
        auto idx = make_unique<IndexInfo>();
        in >> idx->name >> idx->col_idx;
        idx->tree = make_unique<BPlusTree>();
        info->indexes.push_back(move(idx));
      }
    }
    tables_[info->name] = move(info);
  }
}

}  // namespace minidb
