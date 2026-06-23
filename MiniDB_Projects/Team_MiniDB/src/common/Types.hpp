#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace std;

namespace minidb {

// A single cell value stored as text (simple and easy to print in demos).
using Value = string;

// One row = column name -> value.
using Row = map<string, Value>;

// Query result = list of rows.
using RowList = vector<Row>;

// Supported column types in CREATE TABLE.
enum class ColumnType { INT, STRING };

// Comparison operators used in WHERE clauses.
enum class CompareOp { EQ, NE, LT, LE, GT, GE };

// Physical location of a row on disk (page id + slot index).
struct RowLocation {
    uint32_t page_id = 0;
    uint32_t slot_index = 0;
};

// Column definition stored in catalog.
struct ColumnDef {
    string name;
    ColumnType type = ColumnType::INT;
    bool is_primary_key = false;
};

// Table definition stored in catalog.
struct TableDef {
    string name;
    vector<ColumnDef> columns;
    string primary_key_column;
    uint32_t heap_file_id = 0;
};

}  // namespace minidb
