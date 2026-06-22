#ifndef ABSTRACT_EXECUTOR_H
#define ABSTRACT_EXECUTOR_H

#include "common/config.h"
#include "common/rid.h"
#include "parser/query_engine.h" // contains Row, Value

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <variant>
#include <stdexcept>

namespace minidb {

class Column {
public:
    Column(std::string name, std::string type) : name_(std::move(name)), type_(std::move(type)) {}
    const std::string& GetName() const { return name_; }
    const std::string& GetType() const { return type_; }
private:
    std::string name_;
    std::string type_;
};

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> cols) : cols_(std::move(cols)) {}
    const std::vector<Column>& GetColumns() const { return cols_; }
    uint32_t GetColIdx(const std::string& col_name) const {
        for (uint32_t i = 0; i < cols_.size(); ++i) {
            if (cols_[i].GetName() == col_name) return i;
        }
        throw std::runtime_error("Column not found: " + col_name);
    }
private:
    std::vector<Column> cols_;
};

class Tuple {
public:
    Tuple() = default;
    explicit Tuple(Row row) : row_(std::move(row)) {}

    const Row& GetRow() const { return row_; }
    Row& GetRow() { return row_; }

    Value GetValue(const std::string& col_name) const {
        auto it = row_.cols.find(col_name);
        if (it != row_.cols.end()) {
            return it->second;
        }
        throw std::runtime_error("Tuple: column not found: " + col_name);
    }

    Value GetValue(const Schema* schema, uint32_t col_idx) const {
        const auto& col_name = schema->GetColumns()[col_idx].GetName();
        return GetValue(col_name);
    }

private:
    Row row_;
};

class Expression {
public:
    enum class Type { CONSTANT, COLUMN_REF, EQUALS };

    Expression() : type_(Type::CONSTANT), val_(0) {}
    explicit Expression(Value val) : type_(Type::CONSTANT), val_(std::move(val)) {}
    Expression(Type type, std::string col_name) : type_(type), col_name_(std::move(col_name)) {}
    Expression(Type type, std::shared_ptr<Expression> left, std::shared_ptr<Expression> right)
        : type_(type), left_(std::move(left)), right_(std::move(right)) {}

    Value Evaluate(const Tuple* tuple) const {
        if (type_ == Type::CONSTANT) {
            return val_;
        }
        if (type_ == Type::COLUMN_REF) {
            return tuple->GetValue(col_name_);
        }
        if (type_ == Type::EQUALS) {
            Value l_val = left_->Evaluate(tuple);
            Value r_val = right_->Evaluate(tuple);
            return Value(l_val == r_val);
        }
        return Value(0);
    }

private:
    Type type_;
    Value val_;
    std::string col_name_;
    std::shared_ptr<Expression> left_;
    std::shared_ptr<Expression> right_;
};

class AbstractExecutor {
public:
    virtual ~AbstractExecutor() = default;
    virtual void Init() = 0;
    virtual bool Next(Tuple* tuple, RID* rid) = 0;
    virtual void Close() = 0;
    virtual const Schema* GetOutputSchema() const = 0;
};

} // namespace minidb

#endif // ABSTRACT_EXECUTOR_H
