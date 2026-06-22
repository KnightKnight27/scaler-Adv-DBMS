#pragma once

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include "common/types.h"

namespace minidb {

enum class TypeId { 
    INTEGER, 
    VARCHAR 
};

class Column {
public:
    std::string name_;
    TypeId type_;
    uint32_t length_; // Fixed length for INT (4 bytes), max length for VARCHAR

    Column(std::string name, TypeId type, uint32_t length)
        : name_(std::move(name)), type_(type), length_(length) {}
};

class Schema {
private:
    std::vector<Column> columns_;

public:
    explicit Schema(std::vector<Column> columns) : columns_(std::move(columns)) {}

    const std::vector<Column>& GetColumns() const { return columns_; }
    uint32_t GetColumnCount() const { return columns_.size(); }
    const Column& GetColumn(uint32_t index) const { return columns_[index]; }

    // Helper for query execution (e.g., SELECT id FROM users)
    uint32_t GetColIndex(const std::string& col_name) const {
        for (uint32_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].name_ == col_name) {
                return i;
            }
        }
        throw std::logic_error("Column " + col_name + " not found in schema.");
    }
};

} // namespace minidb