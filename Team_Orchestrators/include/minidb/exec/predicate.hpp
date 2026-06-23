#pragma once
// Evaluation of WHERE predicates against a tuple, with numeric coercion.
#include "minidb/schema.hpp"
#include "minidb/sql/ast.hpp"
#include "minidb/types.hpp"

namespace minidb {

// Three-way compare of two values; numeric types (Int/Double) compare numerically.
inline int compare_coerced(const Value& a, const Value& b) {
  bool a_num = a.type() == TypeId::Int || a.type() == TypeId::Double;
  bool b_num = b.type() == TypeId::Int || b.type() == TypeId::Double;
  if (a_num && b_num && a.type() != b.type()) {
    double x = (a.type() == TypeId::Int) ? static_cast<double>(a.as_int()) : a.as_double();
    double y = (b.type() == TypeId::Int) ? static_cast<double>(b.as_int()) : b.as_double();
    return x < y ? -1 : (x > y ? 1 : 0);
  }
  return a.compare(b);
}

inline bool apply_op(CmpOp op, int cmp) {
  switch (op) {
    case CmpOp::Eq: return cmp == 0;
    case CmpOp::Ne: return cmp != 0;
    case CmpOp::Lt: return cmp < 0;
    case CmpOp::Le: return cmp <= 0;
    case CmpOp::Gt: return cmp > 0;
    case CmpOp::Ge: return cmp >= 0;
  }
  return false;
}

// Resolves a (possibly qualified) column reference against a schema whose column
// names may themselves be qualified (e.g. "a.x") after a join. Matching rules:
//   1. exact name match;
//   2. otherwise match by bare column name, requiring the table qualifier to
//      agree when both the reference and the column carry one.
// Returns Schema::npos if unresolved (or ambiguous bare match is taken as first).
inline std::string bare_name(const std::string& s) {
  size_t dot = s.find('.');
  return dot == std::string::npos ? s : s.substr(dot + 1);
}
inline std::string table_qual(const std::string& s) {
  size_t dot = s.find('.');
  return dot == std::string::npos ? std::string() : s.substr(0, dot);
}
inline size_t resolve_column(const Schema& schema, const std::string& ref) {
  size_t exact = schema.index_of(ref);
  if (exact != Schema::npos) return exact;
  std::string rb = bare_name(ref);
  std::string rt = table_qual(ref);
  for (size_t i = 0; i < schema.size(); ++i) {
    const std::string& cn = schema.column(i).name;
    if (bare_name(cn) != rb) continue;
    std::string ct = table_qual(cn);
    if (rt.empty() || ct.empty() || rt == ct) return i;
  }
  return Schema::npos;
}

// Evaluates a conjunction of predicates against a tuple. Throws if a predicate
// names a column not in the schema.
inline bool eval_where(const Schema& schema, const Tuple& t, const WhereClause& where) {
  for (const auto& p : where) {
    size_t idx = resolve_column(schema, p.column);
    if (idx == Schema::npos) throw std::runtime_error("unknown column in WHERE: " + p.column);
    if (!apply_op(p.op, compare_coerced(t[idx], p.value))) return false;
  }
  return true;
}

}  // namespace minidb
