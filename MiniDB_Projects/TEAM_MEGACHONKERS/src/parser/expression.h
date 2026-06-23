#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <stdexcept>
#include <cctype>
#include "common/types.h"
#include "catalog/schema.h"

namespace minidb {

enum class CompareOp { EQ, NE, LT, LE, GT, GE };
enum class LogicOp { AND, OR };

// A resolver maps a (table-qualifier, column-name) reference to a flat column
// index in the row a predicate is evaluated against. The qualifier is empty
// for unqualified references (e.g. `id`). Returns -1 when the column cannot be
// resolved. The planner supplies this so column references can be bound to
// physical positions once, instead of doing name lookups per row.
using ColumnResolver = std::function<int(const std::string& table, const std::string& column)>;

// ---------------------------------------------------------------------------
// Rich expression trees
//
// Every WHERE / ON predicate is represented as a tree of Expression nodes
// rather than a flat (col, op, value) triple. This is what lets the engine
// support arbitrarily nested boolean logic such as
//     WHERE (age > 30 AND role = 'eng') OR id = 1
// A node can be evaluated to a scalar string (Eval) or to a boolean
// (EvalBool); predicates are the boolean-producing nodes.
// ---------------------------------------------------------------------------
class Expression {
public:
    virtual ~Expression() = default;

    // Produce the scalar string value of this sub-expression for a given row.
    virtual std::string Eval(const Row& row, const Schema* schema) const = 0;

    // Evaluate this sub-expression as a predicate (true/false).
    virtual bool EvalBool(const Row& row, const Schema* schema) const = 0;

    // Resolve any column references to physical indices using the resolver.
    virtual void Bind(const ColumnResolver& resolver) = 0;

    // Human-readable reconstruction, used in EXPLAIN-style logging and tests.
    virtual std::string ToString() const = 0;

    virtual std::unique_ptr<Expression> Clone() const = 0;
};

using ExprPtr = std::unique_ptr<Expression>;

// Compares two scalar strings. If both parse cleanly as integers they are
// compared numerically (so "9" < "10"); otherwise lexicographically. This
// keeps the evaluator schema-light while still ordering INTEGER columns
// correctly.
inline int CompareValues(const std::string& a, const std::string& b) {
    auto is_int = [](const std::string& s) -> bool {
        if (s.empty()) return false;
        size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
        if (i == s.size()) return false;
        for (; i < s.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        }
        return true;
    };
    if (is_int(a) && is_int(b)) {
        long long la = std::stoll(a);
        long long lb = std::stoll(b);
        if (la < lb) return -1;
        if (la > lb) return 1;
        return 0;
    }
    return a.compare(b) < 0 ? -1 : (a.compare(b) > 0 ? 1 : 0);
}

inline bool ApplyCompareOp(CompareOp op, int cmp) {
    switch (op) {
        case CompareOp::EQ: return cmp == 0;
        case CompareOp::NE: return cmp != 0;
        case CompareOp::LT: return cmp < 0;
        case CompareOp::LE: return cmp <= 0;
        case CompareOp::GT: return cmp > 0;
        case CompareOp::GE: return cmp >= 0;
    }
    return false;
}

inline std::string CompareOpToString(CompareOp op) {
    switch (op) {
        case CompareOp::EQ: return "=";
        case CompareOp::NE: return "!=";
        case CompareOp::LT: return "<";
        case CompareOp::LE: return "<=";
        case CompareOp::GT: return ">";
        case CompareOp::GE: return ">=";
    }
    return "?";
}

// A literal constant: 'Alice', 42, etc. Stored as its string form.
class ConstantExpression : public Expression {
public:
    explicit ConstantExpression(std::string value) : value_(std::move(value)) {}

    std::string Eval(const Row&, const Schema*) const override { return value_; }
    bool EvalBool(const Row&, const Schema*) const override {
        return !value_.empty() && value_ != "0" && value_ != "false";
    }
    void Bind(const ColumnResolver&) override {}
    std::string ToString() const override { return "'" + value_ + "'"; }
    std::unique_ptr<Expression> Clone() const override {
        return std::make_unique<ConstantExpression>(value_);
    }

    const std::string& value() const { return value_; }

private:
    std::string value_;
};

// A reference to a column, optionally qualified by a table name (orders.id).
// After Bind() it caches the physical index so per-row evaluation is O(1).
class ColumnRefExpression : public Expression {
public:
    ColumnRefExpression(std::string table, std::string column)
        : table_(std::move(table)), column_(std::move(column)) {}

    explicit ColumnRefExpression(std::string column)
        : table_(""), column_(std::move(column)) {}

    std::string Eval(const Row& row, const Schema* schema) const override {
        int idx = ResolveIndex(schema);
        if (idx < 0 || static_cast<size_t>(idx) >= row.columns.size()) {
            throw std::logic_error("Unresolved column reference: " + ToString());
        }
        return row.columns[idx];
    }
    bool EvalBool(const Row& row, const Schema* schema) const override {
        std::string v = Eval(row, schema);
        return !v.empty() && v != "0" && v != "false";
    }
    void Bind(const ColumnResolver& resolver) override {
        bound_index_ = resolver(table_, column_);
    }
    std::string ToString() const override {
        return table_.empty() ? column_ : table_ + "." + column_;
    }
    std::unique_ptr<Expression> Clone() const override {
        auto c = std::make_unique<ColumnRefExpression>(table_, column_);
        c->bound_index_ = bound_index_;
        return c;
    }

    const std::string& table() const { return table_; }
    const std::string& column() const { return column_; }
    int bound_index() const { return bound_index_; }

private:
    std::string table_;
    std::string column_;
    int bound_index_{-1};

    // Use the cached index when bound; otherwise fall back to resolving by name
    // against the schema (handy for unit tests that skip the planner).
    int ResolveIndex(const Schema* schema) const {
        if (bound_index_ >= 0) return bound_index_;
        if (schema == nullptr) return -1;
        try {
            return static_cast<int>(schema->GetColIndex(column_));
        } catch (const std::exception&) {
            return -1;
        }
    }
};

// A comparison predicate: <left> <op> <right>.
class ComparisonExpression : public Expression {
public:
    ComparisonExpression(CompareOp op, ExprPtr left, ExprPtr right)
        : op_(op), left_(std::move(left)), right_(std::move(right)) {}

    std::string Eval(const Row& row, const Schema* schema) const override {
        return EvalBool(row, schema) ? "true" : "false";
    }
    bool EvalBool(const Row& row, const Schema* schema) const override {
        int cmp = CompareValues(left_->Eval(row, schema), right_->Eval(row, schema));
        return ApplyCompareOp(op_, cmp);
    }
    void Bind(const ColumnResolver& resolver) override {
        left_->Bind(resolver);
        right_->Bind(resolver);
    }
    std::string ToString() const override {
        return left_->ToString() + " " + CompareOpToString(op_) + " " + right_->ToString();
    }
    std::unique_ptr<Expression> Clone() const override {
        return std::make_unique<ComparisonExpression>(op_, left_->Clone(), right_->Clone());
    }

    CompareOp op() const { return op_; }
    const Expression* left() const { return left_.get(); }
    const Expression* right() const { return right_.get(); }

private:
    CompareOp op_;
    ExprPtr left_;
    ExprPtr right_;
};

// A boolean combinator: <left> AND/OR <right>. Short-circuits like SQL.
class LogicalExpression : public Expression {
public:
    LogicalExpression(LogicOp op, ExprPtr left, ExprPtr right)
        : op_(op), left_(std::move(left)), right_(std::move(right)) {}

    std::string Eval(const Row& row, const Schema* schema) const override {
        return EvalBool(row, schema) ? "true" : "false";
    }
    bool EvalBool(const Row& row, const Schema* schema) const override {
        if (op_ == LogicOp::AND) {
            return left_->EvalBool(row, schema) && right_->EvalBool(row, schema);
        }
        return left_->EvalBool(row, schema) || right_->EvalBool(row, schema);
    }
    void Bind(const ColumnResolver& resolver) override {
        left_->Bind(resolver);
        right_->Bind(resolver);
    }
    std::string ToString() const override {
        return "(" + left_->ToString() + (op_ == LogicOp::AND ? " AND " : " OR ") +
               right_->ToString() + ")";
    }
    std::unique_ptr<Expression> Clone() const override {
        return std::make_unique<LogicalExpression>(op_, left_->Clone(), right_->Clone());
    }

    LogicOp op() const { return op_; }
    const Expression* left() const { return left_.get(); }
    const Expression* right() const { return right_.get(); }

private:
    LogicOp op_;
    ExprPtr left_;
    ExprPtr right_;
};

} // namespace minidb
