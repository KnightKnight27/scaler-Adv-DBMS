#include "catalog.h"
#include <algorithm>

Catalog::~Catalog() {
    for (auto& kv : tables) {
        delete kv.second->heap;
        delete kv.second->index;
        delete kv.second;
    }
}

void Catalog::createTable(const std::string& name) {
    TableInfo* t = new TableInfo();
    t->name  = name;
    t->heap  = new HeapFile(name + ".db");
    t->index = new BPlusTree();
    tables[name] = t;
}

TableInfo* Catalog::getTable(const std::string& name) {
    auto it = tables.find(name);
    if (it == tables.end()) return nullptr;
    return it->second;
}

void Catalog::recordInsert(const std::string& name, const Row& row) {
    TableInfo* t = getTable(name);
    if (!t) return;
    t->row_count++;
    t->page_count = t->heap->pageCount();

    // Update id stats
    if (t->row_count == 1) {
        t->id_stats.min_val = t->id_stats.max_val = row.id;
        t->age_stats.min_val = t->age_stats.max_val = row.age;
    } else {
        t->id_stats.min_val  = std::min(t->id_stats.min_val,  row.id);
        t->id_stats.max_val  = std::max(t->id_stats.max_val,  row.id);
        t->age_stats.min_val = std::min(t->age_stats.min_val, row.age);
        t->age_stats.max_val = std::max(t->age_stats.max_val, row.age);
    }
    t->id_stats.distinct++;
}

void Catalog::recordDelete(const std::string& name) {
    TableInfo* t = getTable(name);
    if (!t || t->row_count == 0) return;
    t->row_count--;
}
