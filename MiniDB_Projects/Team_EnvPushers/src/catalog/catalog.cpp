#include "catalog/catalog.hpp"

#include <fstream>
#include <sstream>

namespace minidb {

// Text format, one table per record, easy to inspect/debug:
//   TABLE <name> <first_page_id> <num_cols>
//   COL <name> <INT|TEXT> <is_pk 0|1>
//   ...
void Catalog::save(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    for (const auto& t : tables_) {
        out << "TABLE " << t->name << " " << t->first_page_id << " "
            << t->schema.size() << "\n";
        for (const auto& c : t->schema.columns()) {
            out << "COL " << c.name << " "
                << (c.type == TypeId::INTEGER ? "INT" : "TEXT") << " "
                << (c.is_primary_key ? 1 : 0) << "\n";
        }
    }
}

void Catalog::load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return;
    std::string kind;
    while (in >> kind) {
        if (kind != "TABLE") continue;
        std::string name;
        PageId first_page;
        size_t ncols;
        in >> name >> first_page >> ncols;
        std::vector<Column> cols;
        for (size_t i = 0; i < ncols; ++i) {
            std::string ckw, cname, ctype;
            int is_pk;
            in >> ckw >> cname >> ctype >> is_pk;
            Column c;
            c.name = cname;
            c.type = (ctype == "INT") ? TypeId::INTEGER : TypeId::TEXT;
            c.is_primary_key = (is_pk != 0);
            cols.push_back(c);
        }
        auto info = std::make_unique<TableInfo>();
        info->table_id = next_table_id();
        info->name = name;
        info->schema = Schema(std::move(cols));
        info->first_page_id = first_page;
        if (info->schema.primary_key_index() >= 0)
            info->pk_index = std::make_unique<BPlusTree>();
        add_table(std::move(info));
    }
}

}  // namespace minidb
