#ifndef MINIDB_RECORD_H
#define MINIDB_RECORD_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <variant>
#include "Schema.h"

using Value = std::variant<int32_t, std::string>;

struct Record {
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
                if (col.type == DataType::INT) {
                    int32_t intVal = 0;
                    if (std::holds_alternative<int32_t>(val)) {
                        intVal = std::get<int32_t>(val);
                    } else if (std::holds_alternative<std::string>(val)) {
                        try {
                            intVal = std::stoi(std::get<std::string>(val));
                        } catch (...) {}
                    }
                    std::memcpy(dest + offset, &intVal, sizeof(int32_t));
                } else if (col.type == DataType::VARCHAR) {
                    std::string strVal;
                    if (std::holds_alternative<std::string>(val)) {
                        strVal = std::get<std::string>(val);
                    } else if (std::holds_alternative<int32_t>(val)) {
                        strVal = std::to_string(std::get<int32_t>(val));
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
            if (col.type == DataType::INT) {
                int32_t intVal;
                std::memcpy(&intVal, src + offset, sizeof(int32_t));
                r.values.push_back(intVal);
            } else if (col.type == DataType::VARCHAR) {
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
        return deleted_ == other.deleted_ && values == other.values;
    }

    bool operator!=(const Record& other) const {
        return !(*this == other);
    }

    std::string toString() const {
        if (values.size() == 2) {
            std::string s = "Record{id=";
            if (std::holds_alternative<int32_t>(values[0])) {
                s += std::to_string(std::get<int32_t>(values[0]));
            } else {
                s += "'" + std::get<std::string>(values[0]) + "'";
            }
            s += ", val=";
            if (std::holds_alternative<int32_t>(values[1])) {
                s += std::to_string(std::get<int32_t>(values[1]));
            } else {
                s += "'" + std::get<std::string>(values[1]) + "'";
            }
            s += "}";
            return s;
        }
        std::string s = "Record{";
        for (size_t i = 0; i < values.size(); i++) {
            if (i > 0) s += ", ";
            std::visit([&s](const auto& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, int32_t>) {
                    s += std::to_string(arg);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    s += "'" + arg + "'";
                }
            }, values[i]);
        }
        s += "}";
        return s;
    }
};

#endif // MINIDB_RECORD_H
