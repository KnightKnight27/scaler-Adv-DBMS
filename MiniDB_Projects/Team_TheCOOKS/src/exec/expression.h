#pragma once

#include <optional>
#include <string>
#include <vector>

#include "catalog/schema.h"
#include "catalog/value.h"
#include "parser/ast.h"

namespace walterdb {

// A row flowing through the Volcano operator tree: positional Values matching
// the producing operator's ResultSchema.
using Row = std::vector<Value>;

// One output column of an operator, carrying its table qualifier (alias) so
// that qualified references like `users.id` resolve through a join.
struct ColumnMeta {
  std::string table;  // qualifier (alias or table name); "" if none
  std::string name;
  TypeId type;
};

// ---------------------------------------------------------------------------
// ResultSchema -- the column layout of an operator's output.  Richer than a
// storage Schema because intermediate join results carry qualified names so
// `WHERE a.x = b.y` can be resolved positionally.
// ---------------------------------------------------------------------------
class ResultSchema {
 public:
  ResultSchema() = default;

  void add(ColumnMeta c) { columns_.push_back(std::move(c)); }
  size_t size() const { return columns_.size(); }
  const ColumnMeta& at(size_t i) const { return columns_[i]; }
  const std::vector<ColumnMeta>& columns() const { return columns_; }

  // Resolve a (possibly unqualified) column reference to its index.  Sets
  // *ambiguous if more than one column matches an unqualified name.
  std::optional<size_t> resolve(const std::string& table, const std::string& col,
                                bool* ambiguous = nullptr) const;

  static ResultSchema from_table(const Schema& s, const std::string& qualifier);
  ResultSchema concat(const ResultSchema& other) const;

 private:
  std::vector<ColumnMeta> columns_;
};

// ---------------------------------------------------------------------------
// Expression evaluation.
//
//   evaluate()  -- compute an expression's Value for a given row.  Column refs
//                  are resolved by name against `schema`.  Throws on an
//                  unresolved reference (the planner validates first, so at
//                  runtime this is an invariant failure, not user error).
//
//   evaluate_predicate() -- evaluate a WHERE/ON expression with SQL three-valued
//                  logic and collapse the result to a bool: only TRUE passes;
//                  FALSE and NULL (unknown) are filtered out.
// ---------------------------------------------------------------------------
Value evaluate(const Expr* e, const Row& row, const ResultSchema& schema);
bool evaluate_predicate(const Expr* e, const Row& row, const ResultSchema& schema);

// Coerce a literal Value to a target column type for storage (numeric widening,
// integral doubles to ints, NULL stays NULL).  nullopt if incompatible.
std::optional<Value> coerce(const Value& v, TypeId target);

}  // namespace walterdb
