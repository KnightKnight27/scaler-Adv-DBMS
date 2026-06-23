#include "catalog.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace minidb {

Catalog::Catalog(const std::string& dataDir) : dataDir_(dataDir) {
    std::filesystem::create_directories(dataDir_);
    load();
}

std::string Catalog::catalogPath() const { return dataDir_ + "/catalog.txt"; }
std::string Catalog::heapPath(const std::string& name) const { return dataDir_ + "/" + name + ".tbl"; }

bool Catalog::exists(const std::string& name) const { return tables_.count(name) > 0; }

TableInfo* Catalog::get(const std::string& name) {
    auto it = tables_.find(name);
    return it == tables_.end() ? nullptr : it->second.get();
}

std::vector<std::string> Catalog::tableNames() const {
    std::vector<std::string> names;
    for (auto& kv : tables_) names.push_back(kv.first);
    return names;
}

std::unique_ptr<TableInfo> Catalog::openTable(const std::string& name, const Schema& schema) {
    auto t = std::make_unique<TableInfo>();
    t->name = name;
    t->schema = schema;
    t->disk = std::make_unique<DiskManager>(heapPath(name));
    t->pool = std::make_unique<BufferPool>(t->disk.get(), 16);
    t->heap = std::make_unique<HeapFile>(t->disk.get(), t->pool.get());
    if (!schema.columns.empty() && schema.columns[0].type == Type::INT)
        t->index = std::make_unique<BPlusTree>();

    // Rebuild the index and row count from whatever is already on disk.
    int pages = t->disk->numPages();
    for (int pg = 0; pg < pages; ++pg) {
        Page* page = t->pool->fetch(pg);
        for (int s = 0; s < page->numSlots(); ++s) {
            std::string rec;
            if (!page->get(s, rec)) continue;
            Row row = decodeRow(rec);
            t->rowCount++;
            if (t->index && !row.empty() && row[0].isInt())
                t->index->insert(row[0].i, RID{pg, s});
        }
    }
    return t;
}

TableInfo* Catalog::createTable(const std::string& name, const Schema& schema) {
    if (tables_.count(name)) throw std::runtime_error("table '" + name + "' already exists");
    tables_[name] = openTable(name, schema);
    save();
    return tables_[name].get();
}

void Catalog::onInsert(TableInfo* t, const Value& key, RID rid) {
    t->rowCount++;
    if (t->index && key.isInt()) t->index->insert(key.i, rid);
}

void Catalog::onDelete(TableInfo* t, const Value& key) {
    if (t->rowCount > 0) t->rowCount--;
    if (t->index && key.isInt()) t->index->erase(key.i);
}

void Catalog::flush() {
    for (auto& kv : tables_) kv.second->pool->flushAll();
}

static const char* typeName(Type t) { return t == Type::INT ? "INT" : "TEXT"; }

void Catalog::save() const {
    std::ofstream f(catalogPath());
    for (auto& kv : tables_) {
        const TableInfo* t = kv.second.get();
        f << t->name;
        for (const Column& c : t->schema.columns) f << " " << c.name << ":" << typeName(c.type);
        f << "\n";
    }
}

void Catalog::load() {
    std::ifstream f(catalogPath());
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string name;
        ss >> name;
        Schema schema;
        std::string col;
        while (ss >> col) {
            auto colon = col.find(':');
            std::string cname = col.substr(0, colon);
            std::string ctype = col.substr(colon + 1);
            schema.columns.push_back(Column{cname, ctype == "INT" ? Type::INT : Type::TEXT});
        }
        tables_[name] = openTable(name, schema);
    }
}

}  // namespace minidb
