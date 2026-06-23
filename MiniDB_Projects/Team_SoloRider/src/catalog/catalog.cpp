#include "catalog/catalog.h"
#include <fstream>
#include <sstream>
#include <filesystem>

namespace minidb {

Catalog::Catalog(const std::string& db_directory)
    : db_directory_(db_directory) {}

bool Catalog::create_table(const std::string& name, const Schema& schema, int pk_column) {
    // Return false if table already exists
    if (tables_.find(name) != tables_.end()) {
        return false;
    }

    TableInfo info;
    info.table_name = name;
    info.schema = schema;
    info.heap_file_path = db_directory_ + "/" + name + ".db";
    info.primary_key_column = pk_column;
    info.has_index = false;
    info.row_count = 0;
    // Initialize all distinct counts to 0
    info.distinct_counts.assign(schema.column_count(), 0);

    tables_[name] = std::move(info);
    return true;
}

bool Catalog::drop_table(const std::string& name) {
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        return false;
    }
    tables_.erase(it);
    return true;
}

TableInfo* Catalog::get_table(const std::string& name) {
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<std::string> Catalog::get_table_names() const {
    std::vector<std::string> names;
    names.reserve(tables_.size());
    for (const auto& [name, info] : tables_) {
        names.push_back(name);
    }
    return names;
}

void Catalog::update_stats(const std::string& name, uint32_t row_count,
                           const std::vector<uint32_t>& distinct_counts) {
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        return;
    }
    it->second.row_count = row_count;
    it->second.distinct_counts = distinct_counts;
}

void Catalog::set_has_index(const std::string& name, bool has) {
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        return;
    }
    it->second.has_index = has;
}

void Catalog::save_to_disk() const {
    std::filesystem::create_directories(db_directory_);
    std::string path = db_directory_ + "/catalog.dat";
    std::ofstream out(path);
    if (!out.is_open()) {
        return;
    }

    for (const auto& [name, info] : tables_) {
        out << "TABLE:" << info.table_name << "\n";
        out << "HEAP:" << info.heap_file_path << "\n";
        out << "PK:" << info.primary_key_column << "\n";
        out << "HAS_INDEX:" << (info.has_index ? 1 : 0) << "\n";
        out << "ROW_COUNT:" << info.row_count << "\n";

        size_t col_count = info.schema.column_count();
        out << "COLUMNS:" << col_count << "\n";
        for (size_t i = 0; i < col_count; ++i) {
            const Column& col = info.schema.columns[i];
            out << col.name << " "
                << static_cast<int>(col.type) << " "
                << col.max_length << "\n";
        }

        out << "DISTINCT:";
        for (size_t i = 0; i < info.distinct_counts.size(); ++i) {
            if (i > 0) out << ",";
            out << info.distinct_counts[i];
        }
        out << "\n";

        out << "END_TABLE\n";
    }
}

void Catalog::load_from_disk() {
    std::string path = db_directory_ + "/catalog.dat";
    std::ifstream in(path);
    if (!in.is_open()) {
        // File doesn't exist — empty catalog, that's fine
        return;
    }

    tables_.clear();
    std::string line;

    while (std::getline(in, line)) {
        // Expect "TABLE:<name>"
        if (line.substr(0, 6) != "TABLE:") {
            continue;
        }

        TableInfo info;
        info.table_name = line.substr(6);

        // HEAP:<path>
        if (std::getline(in, line) && line.substr(0, 5) == "HEAP:") {
            info.heap_file_path = line.substr(5);
        }

        // PK:<int>
        if (std::getline(in, line) && line.substr(0, 3) == "PK:") {
            info.primary_key_column = std::stoi(line.substr(3));
        }

        // HAS_INDEX:<0|1>
        if (std::getline(in, line) && line.substr(0, 10) == "HAS_INDEX:") {
            info.has_index = (line.substr(10) == "1");
        }

        // ROW_COUNT:<count>
        if (std::getline(in, line) && line.substr(0, 10) == "ROW_COUNT:") {
            info.row_count = static_cast<uint32_t>(std::stoul(line.substr(10)));
        }

        // COLUMNS:<count>
        std::vector<Column> columns;
        if (std::getline(in, line) && line.substr(0, 8) == "COLUMNS:") {
            size_t col_count = std::stoul(line.substr(8));
            for (size_t i = 0; i < col_count; ++i) {
                if (std::getline(in, line)) {
                    std::istringstream iss(line);
                    std::string col_name;
                    int type_int;
                    uint16_t max_length;
                    iss >> col_name >> type_int >> max_length;
                    Column col;
                    col.name = col_name;
                    col.type = static_cast<ColumnType>(type_int);
                    col.max_length = max_length;
                    columns.push_back(std::move(col));
                }
            }
        }
        info.schema = Schema{std::move(columns)};

        // DISTINCT:<d1>,<d2>,...
        if (std::getline(in, line) && line.substr(0, 9) == "DISTINCT:") {
            std::string vals = line.substr(9);
            if (!vals.empty()) {
                std::istringstream dss(vals);
                std::string token;
                while (std::getline(dss, token, ',')) {
                    if (!token.empty()) {
                        info.distinct_counts.push_back(
                            static_cast<uint32_t>(std::stoul(token)));
                    }
                }
            }
        }

        // END_TABLE
        std::getline(in, line); // consume END_TABLE

        tables_[info.table_name] = std::move(info);
    }
}

} // namespace minidb
