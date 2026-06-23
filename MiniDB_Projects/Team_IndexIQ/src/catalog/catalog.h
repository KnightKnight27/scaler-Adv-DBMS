#pragma once
#include "storage/page.h"
#include <string>
#include <vector>
#include <unordered_map>

using Row = std::vector<std::string>;

struct Column {
    std::string name;
    std::string type;
    int size() const { return type == "INT" ? 4 : 256; }
};

struct TableSchema {
    std::string         name;
    std::vector<Column> columns;

    int record_size() const {
        int s = 0;
        for (auto& c : columns) s += c.size();
        return s;
    }

    int slots_per_page() const {
        return static_cast<int>(PAGE_SIZE) / (1 + record_size());
    }

    int col_offset(int idx) const {
        int off = 0;
        for (int i = 0; i < idx; i++) off += columns[i].size();
        return off;
    }

    int col_index(const std::string& col_name) const {
        for (int i = 0; i < (int)columns.size(); i++)
            if (columns[i].name == col_name) return i;
        return -1;
    }
};

void encode_record(const Row& row, const TableSchema& schema, uint8_t* out);

Row decode_record(const uint8_t* data, const TableSchema& schema);

class Catalog {
public:
    explicit Catalog(const std::string& path);

    void                add_table(const TableSchema& schema);
    bool                has_table(const std::string& name) const;
    const TableSchema&  get_schema(const std::string& name) const;
    const std::unordered_map<std::string, TableSchema>& all() const;

private:
    std::string path_;
    std::unordered_map<std::string, TableSchema> tables_;
    void load();
    void save() const;
};
