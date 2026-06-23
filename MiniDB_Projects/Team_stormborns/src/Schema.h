#ifndef MINIDB_SCHEMA_H
#define MINIDB_SCHEMA_H

#include <string>
#include <vector>
#include <stdexcept>

enum class DataType {
    INT,
    VARCHAR
};

inline std::string dataTypeToString(DataType type) {
    if (type == DataType::INT) return "INT";
    if (type == DataType::VARCHAR) return "VARCHAR";
    return "UNKNOWN";
}

struct Column {
    std::string name;
    DataType type;
    int size; // Size in bytes. INT = 4. VARCHAR = N.
};

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> columns) : columns_(std::move(columns)) {}

    const std::vector<Column>& getColumns() const { return columns_; }

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
    std::vector<Column> columns_;
};

#endif // MINIDB_SCHEMA_H
