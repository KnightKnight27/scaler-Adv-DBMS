#include "common/types.h"
#include <cstring>
#include <sstream>

// ============================================================
// DataType helpers
// ============================================================

std::string DataTypeName(DataType t) {
    switch (t) {
        case DataType::INT:     return "INT";
        case DataType::VARCHAR: return "VARCHAR";
        case DataType::BOOL:    return "BOOL";
    }
    return "UNKNOWN";
}

// ============================================================
// Schema
// ============================================================

int Schema::FindColumn(const std::string& name) const {
    for (int i = 0; i < (int)columns.size(); i++) {
        if (columns[i].name == name) return i;
    }
    return -1;
}

// ============================================================
// Value — factory methods
// ============================================================

Value Value::Int(int v) {
    Value val;
    val.type = DataType::INT;
    val.int_val = v;
    return val;
}

Value Value::Varchar(const std::string& v) {
    Value val;
    val.type = DataType::VARCHAR;
    val.str_val = v;
    return val;
}

Value Value::Bool(bool v) {
    Value val;
    val.type = DataType::BOOL;
    val.bool_val = v;
    return val;
}

Value Value::Null(DataType t) {
    Value val;
    val.type = t;
    val.is_null = true;
    return val;
}

// ============================================================
// Value — comparison operators
// ============================================================

bool Value::operator==(const Value& o) const {
    if (is_null || o.is_null) return false;  // NULL != anything (SQL semantics)
    if (type != o.type) return false;
    switch (type) {
        case DataType::INT:     return int_val == o.int_val;
        case DataType::VARCHAR: return str_val == o.str_val;
        case DataType::BOOL:    return bool_val == o.bool_val;
    }
    return false;
}

bool Value::operator!=(const Value& o) const { return !(*this == o); }

bool Value::operator<(const Value& o) const {
    if (is_null || o.is_null) return false;
    switch (type) {
        case DataType::INT:     return int_val < o.int_val;
        case DataType::VARCHAR: return str_val < o.str_val;
        case DataType::BOOL:    return bool_val < o.bool_val;
    }
    return false;
}

bool Value::operator>(const Value& o) const  { return o < *this; }
bool Value::operator<=(const Value& o) const { return !(o < *this); }
bool Value::operator>=(const Value& o) const { return !(*this < o); }

std::string Value::ToString() const {
    if (is_null) return "NULL";
    switch (type) {
        case DataType::INT:     return std::to_string(int_val);
        case DataType::VARCHAR: return "'" + str_val + "'";
        case DataType::BOOL:    return bool_val ? "true" : "false";
    }
    return "?";
}

// ============================================================
// RID
// ============================================================

bool RID::operator==(const RID& o) const {
    return page_id == o.page_id && slot_id == o.slot_id;
}

bool RID::operator<(const RID& o) const {
    if (page_id != o.page_id) return page_id < o.page_id;
    return slot_id < o.slot_id;
}

// ============================================================
// Row Serialization
//
// Format: [null_bitmap (ceil(ncols/8) bytes)] [col data...]
//   INT:     4 bytes (little-endian)
//   BOOL:    1 byte
//   VARCHAR: 2-byte length prefix (LE) + chars
//   NULL cols still have their data area (zeroed)
// ============================================================

static int NullBitmapSize(int num_cols) {
    return (num_cols + 7) / 8;
}

int GetSerializedRowSize(const Row& row, const Schema& schema) {
    int size = NullBitmapSize(schema.columns.size());
    for (int i = 0; i < (int)schema.columns.size(); i++) {
        switch (schema.columns[i].type) {
            case DataType::INT:     size += 4; break;
            case DataType::BOOL:    size += 1; break;
            case DataType::VARCHAR:
                size += 2;  // length prefix
                if (i < (int)row.size() && !row[i].is_null)
                    size += row[i].str_val.size();
                break;
        }
    }
    return size;
}

int SerializeRow(const Row& row, const Schema& schema, char* buf) {
    int ncols = schema.columns.size();
    int bm_size = NullBitmapSize(ncols);
    int offset = 0;

    // Write null bitmap
    memset(buf, 0, bm_size);
    for (int i = 0; i < ncols; i++) {
        if (i < (int)row.size() && row[i].is_null) {
            buf[i / 8] |= (1 << (i % 8));
        }
    }
    offset += bm_size;

    // Write column data
    for (int i = 0; i < ncols; i++) {
        const Value& val = (i < (int)row.size()) ? row[i] : Value::Null(schema.columns[i].type);
        switch (schema.columns[i].type) {
            case DataType::INT: {
                int v = val.is_null ? 0 : val.int_val;
                memcpy(buf + offset, &v, 4);
                offset += 4;
                break;
            }
            case DataType::BOOL: {
                uint8_t v = val.is_null ? 0 : (val.bool_val ? 1 : 0);
                buf[offset] = v;
                offset += 1;
                break;
            }
            case DataType::VARCHAR: {
                uint16_t len = 0;
                if (!val.is_null) len = val.str_val.size();
                memcpy(buf + offset, &len, 2);
                offset += 2;
                if (len > 0) {
                    memcpy(buf + offset, val.str_val.data(), len);
                    offset += len;
                }
                break;
            }
        }
    }
    return offset;
}

Row DeserializeRow(const char* buf, int /*size*/, const Schema& schema) {
    int ncols = schema.columns.size();
    int bm_size = NullBitmapSize(ncols);
    int offset = 0;

    // Read null bitmap
    Row row;
    row.reserve(ncols);

    // Parse bitmap first
    std::vector<bool> nulls(ncols, false);
    for (int i = 0; i < ncols; i++) {
        if (buf[i / 8] & (1 << (i % 8))) {
            nulls[i] = true;
        }
    }
    offset += bm_size;

    // Read column data
    for (int i = 0; i < ncols; i++) {
        if (nulls[i]) {
            row.push_back(Value::Null(schema.columns[i].type));
            // Still need to skip the data bytes
            switch (schema.columns[i].type) {
                case DataType::INT:     offset += 4; break;
                case DataType::BOOL:    offset += 1; break;
                case DataType::VARCHAR: {
                    uint16_t len;
                    memcpy(&len, buf + offset, 2);
                    offset += 2 + len;
                    break;
                }
            }
        } else {
            switch (schema.columns[i].type) {
                case DataType::INT: {
                    int v;
                    memcpy(&v, buf + offset, 4);
                    offset += 4;
                    row.push_back(Value::Int(v));
                    break;
                }
                case DataType::BOOL: {
                    bool v = (buf[offset] != 0);
                    offset += 1;
                    row.push_back(Value::Bool(v));
                    break;
                }
                case DataType::VARCHAR: {
                    uint16_t len;
                    memcpy(&len, buf + offset, 2);
                    offset += 2;
                    std::string s(buf + offset, len);
                    offset += len;
                    row.push_back(Value::Varchar(s));
                    break;
                }
            }
        }
    }
    return row;
}
