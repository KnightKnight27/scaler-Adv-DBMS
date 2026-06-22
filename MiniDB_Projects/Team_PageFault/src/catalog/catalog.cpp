#include "catalog.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace minidb {

Catalog::Catalog(BufferPool& bp, std::string cat_path)
    : bp_(bp), cat_path_(std::move(cat_path)) {}

void Catalog::wireTable(Table* t) {
    // The heap file borrows the table's page list and persists the catalog
    // whenever it appends a page (so the page list stays durable).
    t->heap = std::make_unique<HeapFile>(bp_, t->pages, [this]() { save(); });
    t->index = std::make_unique<BPlusTree>();
}

void Catalog::rebuildIndex(Table* t) const {
    t->index = std::make_unique<BPlusTree>();
    t->heap->scan([&](RowId rid, const char* bytes, uint16_t len) {
        std::vector<Value> row = t->schema.deserialize(std::string(bytes, len));
        t->index->insert(t->schema.primaryKey(row), rid);
    });
}

Table* Catalog::create(const std::string& name, const Schema& schema) {
    if (tables_.count(name)) throw std::runtime_error("table already exists: " + name);
    auto t = std::make_unique<Table>();
    t->name = name;
    t->schema = schema;
    Table* raw = t.get();
    wireTable(raw);
    tables_[name] = std::move(t);
    save();
    return raw;
}

Table* Catalog::get(const std::string& name) const {
    auto it = tables_.find(name);
    return it == tables_.end() ? nullptr : it->second.get();
}

void Catalog::save() const {
    std::ofstream out(cat_path_, std::ios::trunc);
    for (const auto& kv : tables_) {
        const Table* t = kv.second.get();
        out << "TABLE " << t->name << " " << t->schema.size() << "\n";
        for (const Column& c : t->schema.columns())
            out << "COL " << c.name << " " << typeName(c.type) << "\n";
        out << "PAGES " << t->pages.size();
        for (PageId p : t->pages) out << " " << p;
        out << "\n";
    }
}

void Catalog::load() {
    std::ifstream in(cat_path_);
    if (!in.is_open()) return; // fresh database
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string tok;
        ss >> tok;
        if (tok != "TABLE") continue;
        std::string name;
        size_t ncols;
        ss >> name >> ncols;

        std::vector<Column> cols;
        for (size_t i = 0; i < ncols; i++) {
            std::getline(in, line);
            std::istringstream cs(line);
            std::string kw, cname, ctype;
            cs >> kw >> cname >> ctype;
            cols.push_back(Column{cname, ctype == "TEXT" ? Type::TEXT : Type::INT});
        }
        std::getline(in, line);
        std::istringstream ps(line);
        std::string kw;
        size_t npages;
        ps >> kw >> npages;
        std::vector<PageId> pages;
        for (size_t i = 0; i < npages; i++) {
            PageId p;
            ps >> p;
            pages.push_back(p);
        }

        auto t = std::make_unique<Table>();
        t->name = name;
        t->schema = Schema(cols);
        t->pages = pages;
        Table* raw = t.get();
        wireTable(raw);
        tables_[name] = std::move(t);
        rebuildIndex(raw);
    }
}

} // namespace minidb
