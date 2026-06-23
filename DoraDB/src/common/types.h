#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ============================================================
// Data Types supported by DoraDB
// ============================================================

enum class DataType { INT, VARCHAR, BOOL };

// Returns the type name as a string (for printing / error messages)
std::string DataTypeName(DataType t);

// ============================================================
// Column — one column in a table schema
// ============================================================
struct Column {
    std::string name;
    DataType type;
    int max_length = 0;  // only meaningful for VARCHAR(n)
};

// ============================================================
// Schema — list of columns + which one is the primary key
// ============================================================
struct Schema {
    std::vector<Column> columns;
    int pk_index = -1;  // index into columns[], -1 means no PK

    // Find column index by name, returns -1 if not found
    int FindColumn(const std::string& name) const;
};

// ============================================================
// Value — a single cell value in a row
//
// Kept simple on purpose: explicit fields per type instead of
// std::variant, so it's easy to read and debug.
// ============================================================
struct Value {
    DataType type = DataType::INT;
    bool is_null = false;

    int int_val = 0;
    std::string str_val;
    bool bool_val = false;

    // ---- Factory helpers (cleaner than raw constructors) ----
    static Value Int(int v);
    static Value Varchar(const std::string& v);
    static Value Bool(bool v);
    static Value Null(DataType t);

    // ---- Comparisons (for WHERE clause evaluation) ----
    bool operator==(const Value& o) const;
    bool operator!=(const Value& o) const;
    bool operator<(const Value& o) const;
    bool operator>(const Value& o) const;
    bool operator<=(const Value& o) const;
    bool operator>=(const Value& o) const;

    // Print-friendly string
    std::string ToString() const;
};

// A row is just a vector of values, one per column
using Row = std::vector<Value>;

// ============================================================
// RID — Row ID, the stable address of a row in a heap file
//
// B+Tree leaf entries point to these. If a row moves on UPDATE
// (grows past its slot), the old slot stores a forwarding RID
// so the B+Tree entry never needs updating.
// ============================================================
struct RID {
    uint32_t page_id = 0;
    uint16_t slot_id = 0;

    bool operator==(const RID& o) const;
    bool operator<(const RID& o) const;
};

// ============================================================
// Row Serialization — convert Row <-> raw bytes for page storage
//
// Format: [null_bitmap] [col1_data] [col2_data] ...
//   INT   = 4 bytes
//   BOOL  = 1 byte
//   VARCHAR = 2-byte length prefix + chars
//   NULL columns still occupy space (zeroed), bitmap marks them
// ============================================================

// How many bytes this row needs when serialized
int GetSerializedRowSize(const Row& row, const Schema& schema);

// Write row bytes into buffer. Returns bytes written.
int SerializeRow(const Row& row, const Schema& schema, char* buffer);

// Read row bytes from buffer. Returns the row.
Row DeserializeRow(const char* buffer, int size, const Schema& schema);
