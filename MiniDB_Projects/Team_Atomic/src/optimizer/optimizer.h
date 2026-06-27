#pragma once
#include <memory>
#include <string>
#include "catalog/catalog.h"
#include "exec/executor.h"
#include "sql/ast.h"

namespace minidb {

// Statistics gathered (by a scan) to drive cost decisions.
struct TableStats {
  int64_t rows = 0;
  int64_t pk_min = 0;
  int64_t pk_max = 0;
  bool has_pk_index = false;
};

struct PlanResult {
  std::unique_ptr<Executor> root;
  std::string description;  // EXPLAIN-style summary with cost estimates
};

// A cost-based optimizer. For single-table SELECTs it chooses between a full
// table scan and a primary-key index scan based on estimated selectivity. For
// two-table joins it picks the cheaper outer/inner ordering.
class Optimizer {
 public:
  explicit Optimizer(Catalog* catalog) : catalog_(catalog) {}

  PlanResult PlanSelect(const Statement& s);

 private:
  TableStats Analyze(TableMeta* meta);

  // Try to turn PK predicates into an index range [low,high]. Returns true if
  // any usable PK predicate was found; fills `low`/`high` and marks which
  // predicates were consumed (so the rest become a residual filter).
  bool ExtractPkRange(const Statement& s, TableMeta* meta, int64_t* low,
                      int64_t* high, std::vector<bool>* consumed);

  double SelectivityRows(int64_t low, int64_t high, const TableStats& st);

  Catalog* catalog_;
};

}  // namespace minidb
