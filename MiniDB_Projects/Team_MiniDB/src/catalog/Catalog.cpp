#include "catalog/Catalog.hpp"

using namespace std;

namespace minidb {

Catalog::Catalog(PageManager& page_manager) : page_manager_(page_manager) {}

bool Catalog::createTable(const TableDef& def) {
    if (tables_.count(def.name)) return false;
    TableDef stored = def;
    uint32_t page_id = page_manager_.allocatePage(PageType::HEAP);
    stored.heap_file_id = page_id;
    tables_[def.name] = stored;
    heap_files_[def.name] = make_unique<HeapFile>(page_manager_, page_id);
    indexes_[def.name] = make_unique<BPlusTree>();
    return true;
}

const TableDef* Catalog::getTable(const string& name) const {
    auto it = tables_.find(name);
    return it == tables_.end() ? nullptr : &it->second;
}

HeapFile* Catalog::getHeapFile(const string& name) {
    auto it = heap_files_.find(name);
    return it == heap_files_.end() ? nullptr : it->second.get();
}

BPlusTree* Catalog::getPrimaryIndex(const string& name) {
    auto it = indexes_.find(name);
    return it == indexes_.end() ? nullptr : it->second.get();
}

vector<string> Catalog::listTables() const {
    vector<string> names;
    for (const auto& kv : tables_) names.push_back(kv.first);
    return names;
}

}  // namespace minidb
