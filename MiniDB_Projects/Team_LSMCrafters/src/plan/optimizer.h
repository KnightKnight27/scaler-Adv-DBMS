#pragma once
#include "catalog/catalog.h"
#include "exec/operators.h"
#include "sql/ast.h"

namespace minidb {

// Turns a parsed SELECT into a physical operator tree. It is cost-based in two
// ways: it estimates predicate selectivity to choose between a full SeqScan and
// a B+Tree IndexScan, and it orders a two-table join so the smaller relation is
// the outer (fewer inner rescans). The logical shape is fixed for our SQL
// subset: Project( Filter?( Scan | Join(Scan, Scan) ) ).
class Optimizer {
 public:
  explicit Optimizer(Catalog& catalog) : catalog_(catalog) {}

  OperatorPtr plan(const SelectStmt& stmt);

 private:
  // Chooses IndexScan vs SeqScan for a single table given its WHERE predicate.
  OperatorPtr build_scan(TableInfo& table, const Expr* where);

  Catalog& catalog_;
};

}  // namespace minidb
