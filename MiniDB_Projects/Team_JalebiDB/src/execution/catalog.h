#pragma once

#include "common/config.h"
#include "common/types.h"
#include "execution/tuple.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <vector>

namespace minidb {

struct TableMetadata {
    std::string name;
    Schema schema;
    page_id_t first_page_id{INVALID_PAGE_ID};
    page_id_t root_page_id{INVALID_PAGE_ID}; // B+ tree root page
    std::string pk_col; // primary key column name
};

class Catalog {
public:
    Catalog() = default;
    ~Catalog() = default;

    void CreateTable(const std::string &name, const Schema &schema, page_id_t first_page_id, page_id_t root_page_id, const std::string &pk_col) {
        auto meta = std::make_unique<TableMetadata>();
        meta->name = name;
        meta->schema = schema;
        meta->first_page_id = first_page_id;
        meta->root_page_id = root_page_id;
        meta->pk_col = pk_col;
        tables_[name] = std::move(meta);
    }

    TableMetadata *GetTable(const std::string &name) {
        auto iter = tables_.find(name);
        if (iter == tables_.end()) return nullptr;
        return iter->second.get();
    }

    void UpdateTableRoot(const std::string &name, page_id_t root_page_id) {
        auto iter = tables_.find(name);
        if (iter != tables_.end()) {
            iter->second->root_page_id = root_page_id;
        }
    }

    std::vector<std::string> GetTableNames() const {
        std::vector<std::string> names;
        for (const auto &pair : tables_) {
            names.push_back(pair.first);
        }
        return names;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<TableMetadata>> tables_;
};

} // namespace minidb
