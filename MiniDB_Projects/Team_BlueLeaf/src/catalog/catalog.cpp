#include "catalog/catalog.h"

#include <fstream>

#include "common/exception.h"
#include "index/bplus_tree.h"
#include "storage/heap_file.h"

namespace minidb {

Catalog::Catalog(BufferPool* bp, std::string meta_path)
    : bp_(bp), meta_path_(std::move(meta_path)) {
    load();
}

TableInfo* Catalog::create_table(const std::string& name, const Schema& schema, int pk_col) {
    if (has_table(name)) throw DBException("Catalog: table already exists: " + name);

    TableInfo info;
    info.name       = name;
    info.schema     = schema;
    info.pk_col     = pk_col;
    info.heap_first = HeapFile::create(bp_);

    if (pk_col >= 0) {
        PageId root = BPlusTree::create(bp_);
        info.indexes.push_back(IndexInfo{name + "_pk", pk_col, root, /*primary=*/true});
    }

    tables_[name] = std::move(info);
    save();
    return &tables_[name];
}

TableInfo* Catalog::get_table(const std::string& name) {
    auto it = tables_.find(name);
    return it == tables_.end() ? nullptr : &it->second;
}

std::vector<std::string> Catalog::table_names() const {
    std::vector<std::string> names;
    names.reserve(tables_.size());
    for (const auto& [n, _] : tables_) names.push_back(n);
    return names;
}

void Catalog::update_index_root(const std::string& table, const std::string& index, PageId new_root) {
    auto it = tables_.find(table);
    if (it == tables_.end()) return;
    for (auto& ix : it->second.indexes) {
        if (ix.name == index && ix.root != new_root) { ix.root = new_root; save(); return; }
    }
}

void Catalog::save() const {
    std::ofstream out(meta_path_, std::ios::trunc);
    if (!out) throw DBException("Catalog: cannot write metadata " + meta_path_);
    out << tables_.size() << "\n";
    for (const auto& [name, t] : tables_) {
        out << "TABLE " << name << " " << t.schema.num_columns() << " " << t.pk_col << " "
            << t.heap_first << " " << t.indexes.size() << "\n";
        for (const auto& c : t.schema.columns())
            out << "COL " << c.name << " " << static_cast<int>(c.type) << " " << c.length << "\n";
        for (const auto& ix : t.indexes)
            out << "IDX " << ix.name << " " << ix.key_col << " " << ix.root << " "
                << (ix.primary ? 1 : 0) << "\n";
    }
}

void Catalog::load() {
    std::ifstream in(meta_path_);
    if (!in) return;  // fresh database

    std::size_t num_tables = 0;
    in >> num_tables;
    for (std::size_t t = 0; t < num_tables; ++t) {
        std::string tag;
        in >> tag;  // "TABLE"
        TableInfo info;
        std::size_t num_cols = 0, num_idx = 0;
        in >> info.name >> num_cols >> info.pk_col >> info.heap_first >> num_idx;

        std::vector<Column> cols;
        for (std::size_t c = 0; c < num_cols; ++c) {
            std::string ctag, cname;
            int ctype = 0;
            unsigned clen = 0;
            in >> ctag >> cname >> ctype >> clen;
            cols.push_back(Column{cname, static_cast<ValueType>(ctype),
                                  static_cast<std::uint16_t>(clen)});
        }
        info.schema = Schema(std::move(cols));

        for (std::size_t x = 0; x < num_idx; ++x) {
            std::string itag;
            IndexInfo ix;
            int prim = 0;
            in >> itag >> ix.name >> ix.key_col >> ix.root >> prim;
            ix.primary = (prim != 0);
            info.indexes.push_back(ix);
        }
        tables_[info.name] = std::move(info);
    }
}

} // namespace minidb
