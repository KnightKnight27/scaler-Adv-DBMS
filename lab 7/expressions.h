#pragma once

#include "types.h"
#include <string>
#include <stdexcept>
#include <cmath>

struct Expression {
    virtual ~Expression() = default;
    virtual Value evaluate(const Row& row) const = 0;
};

struct Literal : public Expression {
    Value value;

    explicit Literal(Value val) : value(std::move(val)) {}

    Value evaluate(const Row& /*row*/) const override {
        return value;
    }
};

struct ColumnRef : public Expression {
    std::string name;

    explicit ColumnRef(std::string n) : name(std::move(n)) {}

    Value evaluate(const Row& row) const override {
        auto it = row.find(name);
        if (it == row.end()) {
            throw std::runtime_error("Column not found: " + name);
        }
        return it->second;
    }
};

// Helper helpers to handle variant comparison
inline bool compareValues(const Value& lhs, const Value& rhs, const std::string& op) {
    // If they are of different types, try to convert them to double if numeric, otherwise compare as string
    double lNum = 0.0, rNum = 0.0;
    bool lIsNum = false, rIsNum = false;

    if (std::holds_alternative<int>(lhs)) {
        lNum = std::get<int>(lhs);
        lIsNum = true;
    } else if (std::holds_alternative<double>(lhs)) {
        lNum = std::get<double>(lhs);
        lIsNum = true;
    }

    if (std::holds_alternative<int>(rhs)) {
        rNum = std::get<int>(rhs);
        rIsNum = true;
    } else if (std::holds_alternative<double>(rhs)) {
        rNum = std::get<double>(rhs);
        rIsNum = true;
    }

    if (lIsNum && rIsNum) {
        if (op == "=") return std::abs(lNum - rNum) < 1e-9;
        if (op == "!=" || op == "<>") return std::abs(lNum - rNum) >= 1e-9;
        if (op == ">") return lNum > rNum;
        if (op == ">=") return lNum >= rNum;
        if (op == "<") return lNum < rNum;
        if (op == "<=") return lNum <= rNum;
    } else {
        // String comparison
        std::string lStr = std::holds_alternative<std::string>(lhs) ? std::get<std::string>(lhs) : 
                          (std::holds_alternative<int>(lhs) ? std::to_string(std::get<int>(lhs)) : std::to_string(std::get<double>(lhs)));
        std::string rStr = std::holds_alternative<std::string>(rhs) ? std::get<std::string>(rhs) : 
                          (std::holds_alternative<int>(rhs) ? std::to_string(std::get<int>(rhs)) : std::to_string(std::get<double>(rhs)));

        if (op == "=") return lStr == rStr;
        if (op == "!=" || op == "<>") return lStr != rStr;
        if (op == ">") return lStr > rStr;
        if (op == ">=") return lStr >= rStr;
        if (op == "<") return lStr < rStr;
        if (op == "<=") return lStr <= rStr;
    }
    throw std::runtime_error("Unsupported comparison operator: " + op);
}

inline bool getBoolValue(const Value& val) {
    if (std::holds_alternative<int>(val)) return std::get<int>(val) != 0;
    if (std::holds_alternative<double>(val)) return std::abs(std::get<double>(val)) > 1e-9;
    if (std::holds_alternative<std::string>(val)) {
        std::string s = std::get<std::string>(val);
        return s == "true" || s == "1" || s == "TRUE";
    }
    return false;
}

struct BinaryExpression : public Expression {
    std::string op;
    Expression* left;
    Expression* right;

    BinaryExpression(std::string o, Expression* l, Expression* r)
        : op(std::move(o)), left(l), right(r) {}

    ~BinaryExpression() override {
        delete left;
        delete right;
    }

    Value evaluate(const Row& row) const override {
        if (op == "AND") {
            return getBoolValue(left->evaluate(row)) && getBoolValue(right->evaluate(row)) ? 1 : 0;
        }
        if (op == "OR") {
            return getBoolValue(left->evaluate(row)) || getBoolValue(right->evaluate(row)) ? 1 : 0;
        }

        Value lVal = left->evaluate(row);
        Value rVal = right->evaluate(row);
        return compareValues(lVal, rVal, op) ? 1 : 0;
    }
};
