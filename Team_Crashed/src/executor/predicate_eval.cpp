// =============================================================================
// src/executor/predicate_eval.cpp
// -----------------------------------------------------------------------------
// Implementation of the shared predicate-evaluation helpers. See the
// header (include/executor/predicate_eval.h) for the public surface.
//
// This file used to live as an anonymous namespace inside seq_scan.cpp.
// The logic is unchanged; it is only promoted to a regular TU so other
// executors (currently DeleteExecutor) can call into it.
// =============================================================================
#include "executor/predicate_eval.h"

#include <cstdint>
#include <cstring>
#include <span>

#include "catalog/schema.h"
#include "executor/executor.h"
#include "parser/ast.h"

namespace minidb::executor {

// ----- Schema-aware row helpers -----

// Return the byte size of one column's slot inside a row image. Fixed
// scalar types default to 4 bytes; VARCHARs use the declared length.
std::size_t colBytes(const catalog::Column& c) {
    if (c.length == 0) {
        switch (c.type) {
            case catalog::Type::INT:
            case catalog::Type::FLOAT:
            case catalog::Type::BOOL:
                return 4;
            case catalog::Type::VARCHAR:
                return 0;
        }
    }
    return c.length;
}

// Decode a single column's value out of a raw row image.
Value decodeColumn(const catalog::Column& c, const std::uint8_t* base) {
    std::size_t n = colBytes(c);
    switch (c.type) {
        case catalog::Type::INT: {
            int32_t v = 0;
            if (n >= sizeof(int32_t)) std::memcpy(&v, base, sizeof(int32_t));
            return Value::makeInt(v);
        }
        case catalog::Type::FLOAT: {
            float v = 0.0f;
            if (n >= sizeof(float)) std::memcpy(&v, base, sizeof(float));
            return Value::makeFloat(v);
        }
        case catalog::Type::BOOL: {
            int32_t v = 0;
            if (n >= sizeof(int32_t)) std::memcpy(&v, base, sizeof(int32_t));
            return Value::makeBool(v != 0);
        }
        case catalog::Type::VARCHAR: {
            std::string out;
            if (n > 0) out.assign(reinterpret_cast<const char*>(base), n);
            // Trim trailing NULs so VARCHAR columns don't display padding.
            while (!out.empty() && out.back() == '\0') out.pop_back();
            return Value::makeStr(out);
        }
    }
    return Value::makeNull();
}

// Decode a raw row image into a Tuple using the schema's column order.
void decodeRow(std::span<const std::uint8_t> bytes,
               const catalog::Schema& schema,
               Tuple& out) {
    out.values.clear();
    const auto& cols = schema.columns();
    std::size_t off = 0;
    for (const auto& col : cols) {
        std::size_t n = colBytes(col);
        Value v = decodeColumn(col, bytes.data() + off);
        out.values.push_back(v);
        off += n;
    }
}

// Resolve a column by name within a row.
Value resolveColumn(const Tuple& t, const catalog::Schema& schema,
                    const std::string& name) {
    const std::size_t dot = name.find('.');
    const std::string bare = (dot == std::string::npos) ? name : name.substr(dot + 1);
    for (std::size_t i = 0; i < schema.numColumns(); ++i) {
        const std::string& colName = schema.column(i).name;
        const std::size_t colDot = colName.find('.');
        const std::string colBare = (colDot == std::string::npos)
            ? colName
            : colName.substr(colDot + 1);
        const bool matched = (dot == std::string::npos)
            ? (colName == name || colBare == name)
            : (colName == name || (colDot == std::string::npos && colName == bare));
        if (matched && i < t.values.size()) {
            return t.values[i];
        }
    }
    return Value::makeNull();
}

// Evaluate a literal expression to a Value. Anything non-literal falls
// back to NULL.
Value evalLiteral(const parser::Expr& e) {
    switch (e.kind) {
        case parser::ExprKind::INT_LIT:   return Value::makeInt(static_cast<int32_t>(e.intVal));
        case parser::ExprKind::FLOAT_LIT: return Value::makeFloat(static_cast<float>(e.floatVal));
        case parser::ExprKind::STR_LIT:   return Value::makeStr(e.strVal);
        case parser::ExprKind::BOOL_LIT:  return Value::makeBool(e.boolVal);
        case parser::ExprKind::NULL_LIT:  return Value::makeNull();
        default:                          return Value::makeNull();
    }
}

// SQL-ish three-way compare. NULL is treated as "less than everything"
// but reported as 0 so callers can branch on the NULL tag separately.
int compareValues(const Value& a, const Value& b) {
    if (a.tag == Value::Tag::NULL_ || b.tag == Value::Tag::NULL_) return 0;
    if (a.tag == Value::Tag::INT && b.tag == Value::Tag::INT) {
        return (a.i < b.i) ? -1 : (a.i > b.i ? 1 : 0);
    }
    if (a.tag == Value::Tag::FLOAT && b.tag == Value::Tag::FLOAT) {
        return (a.f < b.f) ? -1 : (a.f > b.f ? 1 : 0);
    }
    if (a.tag == Value::Tag::INT && b.tag == Value::Tag::FLOAT) {
        float av = static_cast<float>(a.i);
        return (av < b.f) ? -1 : (av > b.f ? 1 : 0);
    }
    if (a.tag == Value::Tag::FLOAT && b.tag == Value::Tag::INT) {
        float bv = static_cast<float>(b.i);
        return (a.f < bv) ? -1 : (a.f > bv ? 1 : 0);
    }
    if (a.tag == Value::Tag::STRING && b.tag == Value::Tag::STRING) {
        if (a.s == b.s) return 0;
        return a.s < b.s ? -1 : 1;
    }
    if (a.tag == Value::Tag::BOOL && b.tag == Value::Tag::BOOL) {
        return (a.b == b.b) ? 0 : (a.b ? 1 : -1);
    }
    return 0;
}

// Truthiness for boolean / scalar coercion. Used by the predicate evaluator
// when a literal is the top-level expression.
namespace {

bool isTrue(const Value& v) {
    switch (v.tag) {
        case Value::Tag::NULL_:  return false;
        case Value::Tag::BOOL:   return v.b;
        case Value::Tag::INT:    return v.i != 0;
        case Value::Tag::FLOAT:  return v.f != 0.0f;
        case Value::Tag::STRING: return !v.s.empty();
    }
    return false;
}

} // namespace

// Evaluate a predicate over a Tuple. The supported grammar is documented
// in include/executor/predicate_eval.h.
bool evalPredicate(const parser::Expr& e, const Tuple& t,
                   const catalog::Schema& schema) {
    switch (e.kind) {
        case parser::ExprKind::BOOL_LIT:  return e.boolVal;
        case parser::ExprKind::NULL_LIT:  return false;
        case parser::ExprKind::INT_LIT:   return e.intVal != 0;
        case parser::ExprKind::FLOAT_LIT: return e.floatVal != 0.0;
        case parser::ExprKind::STR_LIT:   return !e.strVal.empty();

        case parser::ExprKind::COLUMN: {
            Value v = resolveColumn(t, schema, e.text);
            return isTrue(v);
        }

        case parser::ExprKind::UNARY_OP: {
            if (e.op == "IS NULL" || e.op == "IS NOT NULL") {
                Value v = e.args.empty() ? Value::makeNull()
                                         : resolveColumn(t, schema, e.args[0]->text);
                bool isNull = (v.tag == Value::Tag::NULL_);
                return e.op == "IS NULL" ? isNull : !isNull;
            }
            if (e.op == "NOT") {
                if (!e.args.empty()) {
                    return !evalPredicate(*e.args[0], t, schema);
                }
                return false;
            }
            return true;
        }

        case parser::ExprKind::BINARY_OP: {
            if (e.op == "AND") {
                if (e.args.size() < 2) return true;
                return evalPredicate(*e.args[0], t, schema) &&
                       evalPredicate(*e.args[1], t, schema);
            }
            if (e.op == "OR") {
                if (e.args.size() < 2) return true;
                return evalPredicate(*e.args[0], t, schema) ||
                       evalPredicate(*e.args[1], t, schema);
            }
            if (e.args.size() < 2) return true;
            Value lv = (e.args[0]->kind == parser::ExprKind::COLUMN)
                ? resolveColumn(t, schema, e.args[0]->text)
                : evalLiteral(*e.args[0]);
            Value rv = (e.args[1]->kind == parser::ExprKind::COLUMN)
                ? resolveColumn(t, schema, e.args[1]->text)
                : evalLiteral(*e.args[1]);
            int cmp = compareValues(lv, rv);
            if (e.op == "=")  return cmp == 0;
            if (e.op == "!=") return cmp != 0;
            if (e.op == "<")  return cmp <  0;
            if (e.op == "<=") return cmp <= 0;
            if (e.op == ">")  return cmp >  0;
            if (e.op == ">=") return cmp >= 0;
            return true;
        }

        default: return true;
    }
}

// Deep-clone an Expr tree. We allocate a fresh Expr at every level and
// recurse into args, so the result is fully independent of `e`. We keep
// it here (next to evalPredicate) because the two are the public surface
// of the predicate layer — callers that want one almost always want both.
std::unique_ptr<parser::Expr> cloneExpr(const parser::Expr& e) {
    auto out = std::make_unique<parser::Expr>();
    out->kind     = e.kind;
    out->text     = e.text;
    out->op       = e.op;
    out->intVal   = e.intVal;
    out->floatVal = e.floatVal;
    out->boolVal  = e.boolVal;
    out->strVal   = e.strVal;
    out->line     = e.line;
    out->col      = e.col;
    for (const auto& a : e.args) {
        if (a) out->args.push_back(cloneExpr(*a));
        else   out->args.push_back(nullptr);
    }
    return out;
}

} // namespace minidb::executor
