// ============================================================================
//  executor.hpp — Volcano (iterator) execution model + WHERE evaluation.
//
//  Every operator implements the same pull interface:
//
//       open();  while (next(&tuple)) { ... }
//
//  A query plan is a TREE of these operators; calling next() on the root pulls
//  one tuple, which recursively pulls from children. This is the "Volcano" or
//  iterator model used by Postgres et al. Its virtue: operators compose
//  without knowing each other's internals, and tuples stream one at a time
//  instead of materialising whole intermediate tables.
//
//  Output schema column names are FULLY QUALIFIED ("users.id") so that after a
//  join, a predicate on "users.id" vs "orders.id" stays unambiguous.
// ============================================================================
#pragma once

#include "../catalog/catalog.hpp"
#include "../record/tuple.hpp"
#include "../sql/ast.hpp"

#include <stdexcept>
#include <string>

namespace minidb {

// Resolve a column reference against an output schema. Tries an exact match on
// the qualified name first; if the reference is unqualified, falls back to
// matching the part after the dot. Returns the column index or -1.
inline int find_col(const Schema& s, const std::string& ref) {
    for (int i = 0; i < (int)s.columns.size(); ++i)
        if (s.columns[i].name == ref) return i;        // exact qualified hit
    if (ref.find('.') == std::string::npos) {
        for (int i = 0; i < (int)s.columns.size(); ++i) {
            const std::string& n = s.columns[i].name;
            auto dot = n.find('.');
            if (dot != std::string::npos && n.substr(dot + 1) == ref) return i;
        }
    }
    return -1;
}

// Evaluate a WHERE predicate against a (tuple, schema) pair -> true/false.
// AND/OR recurse; a Compare leaf resolves its column and compares to the
// literal via compare_value (which handles INT vs TEXT).
inline bool eval_predicate(const Expr* e, const Tuple& t, const Schema& s) {
    if (!e) return true;                               // no WHERE => keep all
    switch (e->kind) {
        case Expr::Kind::And: return eval_predicate(e->lhs.get(), t, s) &&
                                     eval_predicate(e->rhs.get(), t, s);
        case Expr::Kind::Or:  return eval_predicate(e->lhs.get(), t, s) ||
                                     eval_predicate(e->rhs.get(), t, s);
        case Expr::Kind::Compare: {
            int c = find_col(s, e->column);
            if (c < 0) throw std::runtime_error("unknown column in WHERE: " + e->column);
            int cmp = compare_value(t.values[c], e->literal);
            switch (e->op) {
                case CmpOp::EQ: return cmp == 0;
                case CmpOp::NE: return cmp != 0;
                case CmpOp::LT: return cmp <  0;
                case CmpOp::LE: return cmp <= 0;
                case CmpOp::GT: return cmp >  0;
                case CmpOp::GE: return cmp >= 0;
            }
        }
    }
    return false;
}

// Abstract operator. out_schema() describes the tuples produced by next().
class Executor {
public:
    virtual ~Executor() = default;
    virtual void open() = 0;
    virtual bool next(Tuple* out) = 0;
    virtual const Schema& out_schema() const = 0;
};

// Build a qualified output schema ("table.col") for a base table.
inline Schema qualify(const std::string& table, const Schema& s) {
    Schema q;
    for (auto& c : s.columns) q.columns.push_back({table + "." + c.name, c.type});
    return q;
}

}  // namespace minidb
