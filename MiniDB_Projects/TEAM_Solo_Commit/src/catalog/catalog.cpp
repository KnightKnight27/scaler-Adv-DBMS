#include "catalog.h"

namespace minidb {

TableInfo* Catalog::CreateTable(const std::string& name, const Schema& schema) {
    if (tables_.count(name)) return nullptr;
    auto info = std::make_unique<TableInfo>();
    info->name = name;
    info->schema = schema;
    info->heap = std::make_unique<HeapFile>(bp_);
    TableInfo* raw = info.get();
    tables_[name] = std::move(info);
    return raw;
}

TableInfo* Catalog::GetTable(const std::string& name) {
    auto it = tables_.find(name);
    return it == tables_.end() ? nullptr : it->second.get();
}

std::vector<std::string> Catalog::ListTables() const {
    std::vector<std::string> out;
    for (const auto& [k, v] : tables_) out.push_back(k);
    return out;
}

}  // namespace minidb
