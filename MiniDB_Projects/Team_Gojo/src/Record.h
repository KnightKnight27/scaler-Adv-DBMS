#ifndef MINIDB_RECORD_H
#define MINIDB_RECORD_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <variant>
#include <vector>
#include "Schema.h"

using Value = std::variant<int, std::string>;

struct Record {
    int _record_id = -1;
    std::vector<Value> values;
    bool deleted_ = false;

    Record() = default;
    explicit Record(std::vector<Value> vals) : values(std::move(vals)), deleted_(false) {}
    
    // For deleted tombstone records
    Record(bool deleted) : deleted_(deleted) {}

    // Compatibility constructors for existing demo codes
    Record(Value v1, Value v2) : deleted_(false) {
        values.push_back(std::move(v1));
        values.push_back(std::move(v2));
    }

    /** Returns true if this slot has been logically deleted. */
    bool isDeleted() const { return deleted_; }

    /** Mark this record as deleted (tombstone). */
    void markDeleted() { deleted_ = true; values.clear(); }

    /**
     * Serialize this record into a raw byte buffer based on the schema.
     */
    void serialize(uint8_t* dest, const Schema& schema) const {
        // First byte: deleted flag (1 if deleted, 0 if active)
        dest[0] = deleted_ ? 1 : 0;
        int offset = 1;
        const auto& columns = schema.getColumns();
        for (size_t i = 0; i < columns.size(); ++i) {
            const auto& col = columns[i];
            if (deleted_ || i >= values.size()) {
                // If deleted or value not provided, write zeros
                std::memset(dest + offset, 0, col.size);
            } else {
                const auto& val = values[i];
                if (col.type == DataType::TYPE_INT) {
                    int32_t intVal = 0;
                    if (std::holds_alternative<int>(val)) {
                        intVal = static_cast<int32_t>(std::get<int>(val));
                    } else if (std::holds_alternative<std::string>(val)) {
                        try {
                            intVal = std::stoi(std::get<std::string>(val));
                        } catch (...) {}
                    }
                    std::memcpy(dest + offset, &intVal, sizeof(int32_t));
                } else if (col.type == DataType::TYPE_VARCHAR) {
                    std::string strVal;
                    if (std::holds_alternative<std::string>(val)) {
                        strVal = std::get<std::string>(val);
                    } else if (std::holds_alternative<int>(val)) {
                        strVal = std::to_string(std::get<int>(val));
                    }
                    int len = std::min(static_cast<int>(strVal.size()), col.size);
                    std::memcpy(dest + offset, strVal.c_str(), len);
                    if (len < col.size) {
                        std::memset(dest + offset + len, 0, col.size - len);
                    }
                }
            }
            offset += col.size;
        }
    }

    /**
     * Deserialize a record from a raw byte buffer based on the schema.
     */
    static Record deserialize(const uint8_t* src, const Schema& schema) {
        Record r;
        r.deleted_ = (src[0] != 0);
        int offset = 1;
        const auto& columns = schema.getColumns();
        for (size_t i = 0; i < columns.size(); ++i) {
            const auto& col = columns[i];
            if (col.type == DataType::TYPE_INT) {
                int32_t intVal;
                std::memcpy(&intVal, src + offset, sizeof(int32_t));
                r.values.push_back(static_cast<int>(intVal));
            } else if (col.type == DataType::TYPE_VARCHAR) {
                std::string strVal(reinterpret_cast<const char*>(src + offset), col.size);
                size_t nullPos = strVal.find('\0');
                if (nullPos != std::string::npos) {
                    strVal = strVal.substr(0, nullPos);
                }
                r.values.push_back(strVal);
            }
            offset += col.size;
        }
        return r;
    }

    bool operator==(const Record& other) const {
        return _record_id == other._record_id && deleted_ == other.deleted_ &&
               values == other.values;
    }

    bool operator!=(const Record& other) const {
        return !(*this == other);
    }

    std::string toString() const {
        std::ostringstream out;
        out << "Record{rid=" << _record_id;
        for (size_t i = 0; i < values.size(); i++) {
            out << ", c" << i << "=";
            std::visit([&out](const auto& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, int>) {
                    out << arg;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    out << "'" << arg << "'";
                }
            }, values[i]);
        }
        out << "}";
        return out.str();
    }
};

#endif // MINIDB_RECORD_H
