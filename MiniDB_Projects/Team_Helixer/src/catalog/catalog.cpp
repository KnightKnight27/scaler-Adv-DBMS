#include "catalog/catalog.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace minidb {

TableInfo *Catalog::install(const std::string &name, const Schema &schema, page_id_t first_page) {
    auto info = std::make_unique<TableInfo>();
    info->name   = name;
    info->schema = schema;
    info->heap   = std::make_unique<TableHeap>(bpm_, &info->schema, first_page);
    // Build a primary-key index only when the pk column is an INTEGER.
    if (schema.pk_index >= 0 && schema.type_of(schema.pk_index) == TypeId::INTEGER) {
        info->index = std::make_unique<BPlusTree>(bpm_);
        info->has_index = true;
    }
    TableInfo *raw = info.get();
    tables_[name] = std::move(info);
    return raw;
}

TableInfo *Catalog::create_table(const std::string &name, const Schema &schema) {
    if (tables_.count(name)) throw std::runtime_error("table already exists: " + name);
    return install(name, schema, INVALID_PAGE_ID);
}

TableInfo *Catalog::get_table(const std::string &name) {
    auto it = tables_.find(name);
    return it == tables_.end() ? nullptr : it->second.get();
}

std::vector<std::string> Catalog::table_names() const {
    std::vector<std::string> out;
    for (auto &kv : tables_) out.push_back(kv.first);
    return out;
}

// Sidecar format (one line per field, easy to read and debug):
//   TABLE <name> <num_cols> <pk_index> <first_page_id>
//   COL <colname> <type:INT|VARCHAR>
//   ... (num_cols COL lines)
void Catalog::save(const std::string &path) const {
    std::ofstream out(path, std::ios::trunc);
    for (auto &kv : tables_) {
        const TableInfo *ti = kv.second.get();
        out << "TABLE " << ti->name << " " << ti->schema.column_count()
            << " " << ti->schema.pk_index
            << " " << ti->heap->first_page_id() << "\n";
        for (const auto &c : ti->schema.columns) {
            out << "COL " << c.name << " "
                << (c.type == TypeId::INTEGER ? "INT" : "VARCHAR") << "\n";
        }
    }
}

void Catalog::load(const std::string &path) {
    std::ifstream in(path);
    if (!in.is_open()) return; // first run, nothing to load
    std::string tok;
    while (in >> tok) {
        if (tok != "TABLE") continue;
        std::string name; int ncols, pk; page_id_t first;
        in >> name >> ncols >> pk >> first;
        Schema schema;
        schema.pk_index = pk;
        for (int i = 0; i < ncols; ++i) {
            std::string kw, cname, ctype;
            in >> kw >> cname >> ctype; // kw == "COL"
            Column col;
            col.name = cname;
            col.type = (ctype == "INT") ? TypeId::INTEGER : TypeId::VARCHAR;
            schema.columns.push_back(col);
        }
        install(name, schema, first);
    }
}

void Catalog::rebuild_indexes() {
    for (auto &kv : tables_) {
        TableInfo *ti = kv.second.get();
        auto rows = ti->heap->scan();
        ti->row_count = rows.size();
        if (!ti->has_index) continue;
        int pk = ti->schema.pk_index;
        for (auto &rt : rows) {
            int32_t key = rt.second[pk].as_int();
            ti->index->insert(key, rt.first);
        }
    }
}

} // namespace minidb
