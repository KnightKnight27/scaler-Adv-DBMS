#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "common/types.h"

namespace minidb {

struct TableInfo {
    std::string table_name;
    Schema schema;
    std::string heap_file_path;
    int primary_key_column = -1;
    bool has_index = false;
    uint32_t row_count = 0;
    std::vector<uint32_t> distinct_counts;
};

class Catalog {
public:
    explicit Catalog(const std::string& db_directory);
    bool create_table(const std::string& name, const Schema& schema, int pk_column = -1);
    bool drop_table(const std::string& name);
    TableInfo* get_table(const std::string& name);
    std::vector<std::string> get_table_names() const;
    void update_stats(const std::string& name, uint32_t row_count, const std::vector<uint32_t>& distinct_counts);
    void set_has_index(const std::string& name, bool has);
    void save_to_disk() const;
    void load_from_disk();

private:
    std::unordered_map<std::string, TableInfo> tables_;
    std::string db_directory_;
};

} // namespace minidb
