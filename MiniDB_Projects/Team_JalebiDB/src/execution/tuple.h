#pragma once

#include "common/config.h"
#include "common/types.h"
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>

namespace minidb {

enum class TypeId {
    INT,
    VARCHAR
};

class Value {
public:
    Value() = default;
    explicit Value(int32_t val) : type_(TypeId::INT), int_val_(val) {}
    explicit Value(const std::string &val) : type_(TypeId::VARCHAR), str_val_(val) {}

    TypeId GetType() const { return type_; }
    int32_t GetInt() const { return int_val_; }
    const std::string &GetStr() const { return str_val_; }

    bool operator==(const Value &other) const {
        if (type_ != other.type_) return false;
        if (type_ == TypeId::INT) return int_val_ == other.int_val_;
        return str_val_ == other.str_val_;
    }

    bool operator<(const Value &other) const {
        if (type_ != other.type_) return false;
        if (type_ == TypeId::INT) return int_val_ < other.int_val_;
        return str_val_ < other.str_val_;
    }

    bool operator>(const Value &other) const {
        return other < *this;
    }

    bool operator<=(const Value &other) const {
        return !(other < *this);
    }

    bool operator>=(const Value &other) const {
        return !(*this < other);
    }

    std::string ToString() const {
        if (type_ == TypeId::INT) {
            return std::to_string(int_val_);
        }
        return str_val_;
    }

private:
    TypeId type_{TypeId::INT};
    int32_t int_val_{0};
    std::string str_val_{""};
};

struct Column {
    std::string name;
    TypeId type;
    uint32_t length{0}; // only for VARCHAR
};

class Schema {
public:
    Schema() = default;
    explicit Schema(const std::vector<Column> &cols) : columns_(cols) {}

    const std::vector<Column> &GetColumns() const { return columns_; }
    
    int GetColIdx(const std::string &col_name) const {
        // Strip table prefix if present (e.g., "t1.id" -> "id")
        std::string name = col_name;
        size_t dot_pos = name.find('.');
        if (dot_pos != std::string::npos) {
            name = name.substr(dot_pos + 1);
        }

        for (size_t i = 0; i < columns_.size(); ++i) {
            std::string col_base = columns_[i].name;
            size_t c_dot = col_base.find('.');
            if (c_dot != std::string::npos) {
                col_base = col_base.substr(c_dot + 1);
            }
            if (col_base == name) return static_cast<int>(i);
        }
        return -1;
    }

private:
    std::vector<Column> columns_;
};

class Tuple {
public:
    Tuple() = default;
    explicit Tuple(const std::vector<Value> &values) : values_(values) {}

    const std::vector<Value> &GetValues() const { return values_; }
    Value GetValue(const Schema &schema, int col_idx) const {
        return values_.at(col_idx);
    }

    // Serialise tuple into a byte string
    std::string Serialize(const Schema &schema) const {
        std::string out;
        for (size_t i = 0; i < schema.GetColumns().size(); ++i) {
            const auto &col = schema.GetColumns()[i];
            const Value &val = values_[i];
            if (col.type == TypeId::INT) {
                int32_t int_val = val.GetInt();
                out.append(reinterpret_cast<const char *>(&int_val), sizeof(int32_t));
            } else if (col.type == TypeId::VARCHAR) {
                std::string str_val = val.GetStr();
                uint16_t len = static_cast<uint16_t>(str_val.size());
                out.append(reinterpret_cast<const char *>(&len), sizeof(uint16_t));
                out.append(str_val.data(), len);
            }
        }
        return out;
    }

    // Deserialise tuple from a byte string
    static Tuple Deserialize(const std::string &data, const Schema &schema) {
        std::vector<Value> values;
        int offset = 0;
        for (const auto &col : schema.GetColumns()) {
            if (col.type == TypeId::INT) {
                if (offset + static_cast<int>(sizeof(int32_t)) > static_cast<int>(data.size())) {
                    throw std::runtime_error("Deserialization offset overflow for INT");
                }
                int32_t int_val;
                std::memcpy(&int_val, data.data() + offset, sizeof(int32_t));
                values.push_back(Value(int_val));
                offset += sizeof(int32_t);
            } else if (col.type == TypeId::VARCHAR) {
                if (offset + static_cast<int>(sizeof(uint16_t)) > static_cast<int>(data.size())) {
                    throw std::runtime_error("Deserialization offset overflow for VARCHAR len");
                }
                uint16_t len;
                std::memcpy(&len, data.data() + offset, sizeof(uint16_t));
                offset += sizeof(uint16_t);
                if (offset + len > data.size()) {
                    throw std::runtime_error("Deserialization offset overflow for VARCHAR data");
                }
                std::string str_val(data.data() + offset, len);
                values.push_back(Value(str_val));
                offset += len;
            }
        }
        return Tuple(values);
    }

private:
    std::vector<Value> values_;
};

} // namespace minidb
