#include "catalog/catalog.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>

Catalog::Catalog(const std::string& path) : path_(path) { load(); }

void Catalog::load() {
    std::ifstream f(path_);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        TableSchema s;
        ss >> s.name;
        std::string token;
        while (ss >> token) {
            auto colon = token.find(':');
            Column c;
            c.name = token.substr(0, colon);
            c.type = token.substr(colon + 1);
            s.columns.push_back(c);
        }
        tables_[s.name] = s;
    }
}

void Catalog::save() const {
    std::ofstream f(path_);
    for (auto& [name, s] : tables_) {
        f << s.name;
        for (auto& c : s.columns) f << " " << c.name << ":" << c.type;
        f << "\n";
    }
}

void Catalog::add_table(const TableSchema& schema) {
    tables_[schema.name] = schema;
    save();
}

bool Catalog::has_table(const std::string& name) const {
    return tables_.count(name) > 0;
}

const TableSchema& Catalog::get_schema(const std::string& name) const {
    auto it = tables_.find(name);
    if (it == tables_.end()) throw std::runtime_error("Table not found: " + name);
    return it->second;
}

const std::unordered_map<std::string, TableSchema>& Catalog::all() const {
    return tables_;
}

void encode_record(const Row& row, const TableSchema& schema, uint8_t* out) {
    for (int i = 0; i < (int)schema.columns.size(); i++) {
        int offset = schema.col_offset(i);
        if (schema.columns[i].type == "INT") {
            int v = std::stoi(row[i]);
            std::memcpy(out + offset, &v, 4);
        } else {
            std::memset(out + offset, 0, 256);
            std::memcpy(out + offset, row[i].c_str(),
                        std::min((int)row[i].size(), 255));
        }
    }
}

Row decode_record(const uint8_t* data, const TableSchema& schema) {
    Row row;
    for (int i = 0; i < (int)schema.columns.size(); i++) {
        int offset = schema.col_offset(i);
        if (schema.columns[i].type == "INT") {
            int v;
            std::memcpy(&v, data + offset, 4);
            row.push_back(std::to_string(v));
        } else {
            row.push_back(std::string(reinterpret_cast<const char*>(data + offset)));
        }
    }
    return row;
}
