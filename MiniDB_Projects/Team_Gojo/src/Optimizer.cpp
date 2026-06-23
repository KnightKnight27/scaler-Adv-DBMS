#include "Optimizer.h"

#include <cmath>
#include <sstream>

#include "FilterNode.h"
#include "IndexScanNode.h"
#include "NestedLoopJoinNode.h"
#include "TableScanNode.h"

// ── Constructor ─────────────────────────────────────────────────────────

Optimizer::Optimizer(Catalog &catalog) : catalog_(catalog) {}

// ── Main optimization entry point ───────────────────────────────────────

std::unique_ptr<PlanNode> Optimizer::optimize(ASTNode *ast) {
  explanation_.clear();

  // Dispatch based on AST node type
  if (auto *select = dynamic_cast<SelectNode *>(ast)) {
    return optimizeSelect(select);
  }

  // INSERT and DELETE don't produce execution plans in the Volcano model —
  // they're handled directly by the executor. Return nullptr to signal this.
  return nullptr;
}

// ── SELECT optimization ─────────────────────────────────────────────────

std::unique_ptr<PlanNode> Optimizer::optimizeSelect(SelectNode *select) {
  std::ostringstream explain;

  // ────────────────────────────────────────────────────────────────
  // CASE 1: Simple SELECT with optional WHERE (no JOIN)
  // ────────────────────────────────────────────────────────────────
  if (!select->hasJoin) {
    Table *table = catalog_.getTable(select->tableName);
    auto stats = catalog_.getStats(select->tableName);

    if (select->hasWhere) {
      /*
       * ACCESS PATH DECISION:
       * Compare the cost of two alternatives:
       *   A) IndexScan  — if predicate is on indexed column (id) with =
       *   B) TableScan + Filter — for everything else
       *
       * The optimizer picks the cheaper option.
       */
      double selectivity = estimateSelectivity(
          select->tableName, select->whereCol, select->whereOp);

      double tableScanCost = estimateTableScanCost(select->tableName);
      double indexScanCost = estimateIndexScanCost();

      // Index scan is only applicable for equality on the primary key
      bool canUseIndex = isIndexedColumn(select->whereCol) &&
                         select->whereOp == CompOp::EQUALS;

      explain << "=== CBO Decision ===\n";
      explain << "Table: " << select->tableName << " (" << stats.numRows
              << " rows)\n";
      explain << "Predicate: " << select->whereCol << " "
              << (select->whereOp == CompOp::EQUALS ? "="
                  : select->whereOp == CompOp::LESS ? "<"
                                                    : ">")
              << " " << select->whereVal << "\n";
      explain << "Selectivity: " << selectivity << "\n";
      explain << "TableScan cost: " << tableScanCost << "\n";
      explain << "IndexScan cost: " << indexScanCost << "\n";

      if (canUseIndex && indexScanCost < tableScanCost) {
        explain << "DECISION: IndexScan (cost " << indexScanCost << " < "
                << tableScanCost << ")\n";
        explanation_ = explain.str();

        return std::unique_ptr<PlanNode>(new IndexScanNode(table, select->whereVal));
      } else {
        explain << "DECISION: TableScan + Filter\n";
        if (canUseIndex) {
          explain << "  (Index available but TableScan cheaper "
                  << "due to high selectivity)\n";
        } else {
          explain << "  (No index on column '" << select->whereCol << "')\n";
        }
        explanation_ = explain.str();

        auto scan = std::make_unique<TableScanNode>(table);
        return std::make_unique<FilterNode>(std::move(scan), select->whereCol,
                                            select->whereOp, Value{select->whereVal});
      }
    } else {
      // No WHERE clause — must do a full table scan
      explain << "=== CBO Decision ===\n";
      explain << "No predicate → full TableScan\n";
      explanation_ = explain.str();

      return std::make_unique<TableScanNode>(table);
    }
  }

  // ────────────────────────────────────────────────────────────────
  // CASE 2: JOIN query
  // ────────────────────────────────────────────────────────────────
  Table *table1 = catalog_.getTable(select->tableName);
  Table *table2 = catalog_.getTable(select->joinTable);

  auto stats1 = catalog_.getStats(select->tableName);
  auto stats2 = catalog_.getStats(select->joinTable);

  /*
   * JOIN ORDER SELECTION:
   * For nested loop join, the cost is:
   *   cost = |outer| + |outer| × |inner|
   *
   * We want the smaller table on the outer side to minimize
   * the number of full inner-table scans.
   *
   * Compare:
   *   costAB = |A| + |A| × |B|
   *   costBA = |B| + |B| × |A|
   */
  double costAB = stats1.numRows + (double)stats1.numRows * stats2.numRows;
  double costBA = stats2.numRows + (double)stats2.numRows * stats1.numRows;

  explain << "=== CBO Join Decision ===\n";
  explain << "Table " << select->tableName << ": " << stats1.numRows
          << " rows\n";
  explain << "Table " << select->joinTable << ": " << stats2.numRows
          << " rows\n";
  explain << "Cost(" << select->tableName << " outer): " << costAB << "\n";
  explain << "Cost(" << select->joinTable << " outer): " << costBA << "\n";

  std::unique_ptr<PlanNode> outerScan, innerScan;
  std::string outerCol, innerCol;

  if (costAB <= costBA) {
    // table1 is outer (smaller or equal)
    explain << "DECISION: " << select->tableName << " as outer (cost " << costAB
            << " <= " << costBA << ")\n";

    outerScan = std::make_unique<TableScanNode>(table1);
    innerScan = std::make_unique<TableScanNode>(table2);

    // Map join columns: find which col belongs to which table
    if (select->joinTable1 == select->tableName) {
      outerCol = select->joinCol1;
      innerCol = select->joinCol2;
    } else {
      outerCol = select->joinCol2;
      innerCol = select->joinCol1;
    }
  } else {
    // table2 is outer (smaller)
    explain << "DECISION: " << select->joinTable << " as outer (cost " << costBA
            << " < " << costAB << ")\n";

    outerScan = std::make_unique<TableScanNode>(table2);
    innerScan = std::make_unique<TableScanNode>(table1);

    if (select->joinTable2 == select->joinTable) {
      outerCol = select->joinCol2;
      innerCol = select->joinCol1;
    } else {
      outerCol = select->joinCol1;
      innerCol = select->joinCol2;
    }
  }

  auto joinNode = std::make_unique<NestedLoopJoinNode>(
      std::move(outerScan), std::move(innerScan), outerCol, innerCol);

  // If there's a WHERE clause on the join result, add a filter
  if (select->hasWhere) {
    explain << "Adding Filter: " << select->whereCol << " "
            << (select->whereOp == CompOp::EQUALS ? "="
                : select->whereOp == CompOp::LESS ? "<"
                                                  : ">")
            << " " << select->whereVal << "\n";

    explanation_ = explain.str();

    return std::make_unique<FilterNode>(std::move(joinNode), select->whereCol,
                                        select->whereOp, Value{select->whereVal});
  }

  explanation_ = explain.str();
  return joinNode;
}

// ── Selectivity estimation ──────────────────────────────────────────────

double Optimizer::estimateSelectivity(const std::string &tableName,
                                      const std::string &col, CompOp op) {
  auto stats = catalog_.getStats(tableName);

  switch (op) {
  case CompOp::EQUALS:
    /*
     * EQUALITY SELECTIVITY = 1 / numDistinct
     *
     * Assumes uniform distribution of values. If there are 1000
     * distinct values, each value appears in ~1/1000 of the rows.
     *
     * This is the same formula used by PostgreSQL's cost estimator
     * when histogram data isn't available (eqsel() function).
     */
    return 1.0 / stats.numDistinct;

  case CompOp::LESS:
  case CompOp::GREATER:
    /*
     * RANGE SELECTIVITY = 1/3 (magic constant)
     *
     * This is a well-known default heuristic used when we don't
     * have histogram data. PostgreSQL uses 0.3333 as the default
     * selectivity for inequality operators without statistics.
     *
     * With histograms, we could compute the exact fraction of
     * values in the range, but that requires maintaining detailed
     * value distribution data (ANALYZE command).
     */
    return 1.0 / 3.0;

  default:
    return 1.0; // no predicate → all rows pass
  }
}

double Optimizer::estimateTableScanCost(const std::string &tableName) {
  /*
   * Table scan cost = number of data pages to read.
   * Each page holds up to 511 records, so:
   *   numPages ≈ ceil(numRows / 511)
   */
  auto stats = catalog_.getStats(tableName);
  return std::ceil(static_cast<double>(stats.numRows) /
                   511);
}

double Optimizer::estimateIndexScanCost() {
  /*
   * Index scan cost = height of B+ tree ≈ 3
   *
   * For our B+ tree with order 510:
   *   - Height 1 (root only): up to 510 keys
   *   - Height 2: up to ~260,000 keys
   *   - Height 3: up to ~132 million keys
   *
   * For any reasonable workload, 3 page reads suffices. Plus 1 page
   * read for the actual data page = total cost of ~4.
   *
   * In a production optimizer, we'd compute the actual tree height
   * from the B+ tree metadata.
   */
  return 4.0;
}

bool Optimizer::isIndexedColumn(const std::string &col) {
  /*
   * In MiniDB, only the "id" column has a B+ tree index (primary key).
   * The "val" column is not indexed.
   *
   * A production database would consult the catalog's index metadata
   * to determine which columns have indexes.
   */
  return col == "id";
}
