#ifndef MINIDB_SCHEMA_H
#define MINIDB_SCHEMA_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

enum class DataType {
    TYPE_INT,
    TYPE_VARCHAR,

    // Backward-compatible aliases used by older demo code.
    INT = TYPE_INT,
    VARCHAR = TYPE_VARCHAR
};

inline std::string dataTypeToString(DataType type) {
    if (type == DataType::TYPE_INT) return "INT";
    if (type == DataType::TYPE_VARCHAR) return "VARCHAR";
    return "UNKNOWN";
}

inline DataType dataTypeFromString(const std::string& type) {
    if (type == "INT" || type == "TYPE_INT") return DataType::TYPE_INT;
    if (type == "VARCHAR" || type == "TYPE_VARCHAR") return DataType::TYPE_VARCHAR;
    throw std::runtime_error("Unknown data type: " + type);
}

inline int defaultSizeForType(DataType type) {
    if (type == DataType::TYPE_INT) return static_cast<int>(sizeof(int32_t));
    if (type == DataType::TYPE_VARCHAR) return 256;
    throw std::runtime_error("Unknown data type");
}

struct ColumnDef {
    std::string name;
    DataType type;
    int size; // Fixed page-storage width. INT = 4, VARCHAR defaults to 256.

    ColumnDef() : type(DataType::TYPE_INT), size(defaultSizeForType(DataType::TYPE_INT)) {}
    ColumnDef(std::string columnName, DataType columnType)
        : name(std::move(columnName)), type(columnType), size(defaultSizeForType(columnType)) {}
    ColumnDef(std::string columnName, DataType columnType, int columnSize)
        : name(std::move(columnName)), type(columnType), size(columnSize) {}
};

using Column = ColumnDef;

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<ColumnDef> columns) : columns_(std::move(columns)) {}

    const std::vector<ColumnDef>& getColumns() const { return columns_; }

    bool empty() const { return columns_.empty(); }
    size_t size() const { return columns_.size(); }
    const ColumnDef& operator[](size_t idx) const { return columns_.at(idx); }

    int getRecordSize() const {
        // Record size is 1 byte (deleted/tombstone marker) + sum of column sizes
        int total = 1;
        for (const auto& col : columns_) {
            total += col.size;
        }
        return total;
    }

    int getColumnIndex(const std::string& name) const {
        for (size_t i = 0; i < columns_.size(); i++) {
            if (columns_[i].name == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

private:
    std::vector<ColumnDef> columns_;
};

#endif // MINIDB_SCHEMA_H
