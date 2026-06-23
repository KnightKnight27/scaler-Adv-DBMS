#include "catalog/catalog.h"

namespace minidb {

TableInfo* Catalog::CreateTable(const std::string& name, const Schema& schema,
                                BufferPoolManager* bpm) {
    if (tables_.count(name)) return nullptr; // already exists

    auto info = std::make_unique<TableInfo>();
    info->name    = name;
    info->schema  = schema;
    info->heap_file    = std::make_unique<HeapFile>(name, bpm, schema);
    info->primary_index = std::make_unique<BPlusTree>(64);
    info->pk_col  = 0; // first column is always the primary key

    info->heap_file->ScanAll([&](const RecordId& rid, const Row& row) {
        if (!row.empty() && std::holds_alternative<int64_t>(row[0])) {
            info->primary_index->Insert(std::get<int64_t>(row[0]), rid);
        }
    });

    TableInfo* ptr = info.get();
    tables_[name] = std::move(info);
    return ptr;
}

TableInfo* Catalog::GetTable(const std::string& name) {
    auto it = tables_.find(name);
    return (it != tables_.end()) ? it->second.get() : nullptr;
}

std::vector<std::string> Catalog::GetTableNames() const {
    std::vector<std::string> names;
    for (auto& [k, v] : tables_) {
        names.push_back(k);
    }
    return names;
}

} // namespace minidb
