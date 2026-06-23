#include "catalog/catalog.h"
#include <fstream>
#include <sstream>
#include <filesystem>

Catalog::Catalog(const std::string& catalog_file) : catalog_file_(catalog_file) {
    if (std::filesystem::exists(catalog_file_)) {
        Load();
    }
}

bool Catalog::CreateTable(const std::string& name, const Schema& schema,
                          int first_page_id, const std::string& index_file) {
    if (tables_.count(name)) return false;
    TableInfo info;
    info.name = name;
    info.schema = schema;
    info.first_page_id = first_page_id;
    info.index_file = index_file;
    tables_[name] = info;
    Save();
    return true;
}

const TableInfo* Catalog::GetTable(const std::string& name) const {
    auto it = tables_.find(name);
    if (it == tables_.end()) return nullptr;
    return &it->second;
}

std::vector<std::string> Catalog::GetAllTableNames() const {
    std::vector<std::string> names;
    for (auto& [name, _] : tables_) names.push_back(name);
    return names;
}

// ============================================================
// Persistence — simple line-based text format
//
// Format:
//   TABLE:<name>
//   HEAP:<first_page_id>
//   IDX:<index_file>
//   PK:<pk_index>
//   COL:<name>:<type>:<max_length>
//   ---
// ============================================================

void Catalog::Save() {
    std::filesystem::path p(catalog_file_);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());

    std::ofstream out(catalog_file_);
    for (auto& [name, info] : tables_) {
        out << "TABLE:" << info.name << "\n";
        out << "HEAP:" << info.first_page_id << "\n";
        out << "IDX:" << info.index_file << "\n";
        out << "PK:" << info.schema.pk_index << "\n";
        for (auto& col : info.schema.columns) {
            out << "COL:" << col.name << ":" << (int)col.type << ":" << col.max_length << "\n";
        }
        out << "---\n";
    }
}

void Catalog::Load() {
    std::ifstream in(catalog_file_);
    if (!in.is_open()) return;

    std::string line;
    TableInfo current;
    bool in_table = false;

    while (std::getline(in, line)) {
        if (line.substr(0, 6) == "TABLE:") {
            current = TableInfo();
            current.name = line.substr(6);
            in_table = true;
        } else if (line.substr(0, 5) == "HEAP:" && in_table) {
            current.first_page_id = std::stoi(line.substr(5));
        } else if (line.substr(0, 4) == "IDX:" && in_table) {
            current.index_file = line.substr(4);
        } else if (line.substr(0, 3) == "PK:" && in_table) {
            current.schema.pk_index = std::stoi(line.substr(3));
        } else if (line.substr(0, 4) == "COL:" && in_table) {
            // COL:name:type:max_length
            std::istringstream ss(line.substr(4));
            std::string name, type_str, len_str;
            std::getline(ss, name, ':');
            std::getline(ss, type_str, ':');
            std::getline(ss, len_str, ':');
            Column col;
            col.name = name;
            col.type = static_cast<DataType>(std::stoi(type_str));
            col.max_length = std::stoi(len_str);
            current.schema.columns.push_back(col);
        } else if (line == "---" && in_table) {
            tables_[current.name] = current;
            in_table = false;
        }
    }
}
