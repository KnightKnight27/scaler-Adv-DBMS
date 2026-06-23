#ifndef MINIDB_OPTIMIZER_H
#define MINIDB_OPTIMIZER_H

#include <memory>
#include <string>

#include "Catalog.h"
#include "Parser.h"
#include "PlanNode.h"

/**
 * Cost-Based Optimizer (CBO) for MiniDB.
 *
 * ═══════════════════════════════════════════════════════════════════════
 * ROLE IN THE QUERY PIPELINE:
 *   SQL → [Lexer] → Tokens → [Parser] → AST → [Optimizer] → Plan Tree
 *
 * The optimizer transforms a LOGICAL query (AST) into a PHYSICAL
 * execution plan (tree of PlanNodes) by making three key decisions:
 *
 * 1. ACCESS PATH SELECTION: Table scan vs. index scan
 * 2. JOIN ORDER: Which table goes on the outer loop
 * 3. PREDICATE PLACEMENT: Where to put filter operators
 *
 * ═══════════════════════════════════════════════════════════════════════
 * SELECTIVITY ESTIMATION
 * ═══════════════════════════════════════════════════════════════════════
 *
 * Selectivity (σ) estimates what fraction of rows will pass a predicate.
 *
 * - Equality (col = val):  σ = 1/numDistinct
 *   Assumes uniform distribution. If numDistinct = 1000, then ~0.1% of
 *   rows match any specific value.
 *
 * - Range (col < val):     σ = 1/3
 *   Default heuristic when we don't have histogram data. PostgreSQL
 *   uses histograms in pg_statistic for much more accurate estimates.
 *
 * - No predicate:          σ = 1.0
 *   All rows pass.
 *
 * ═══════════════════════════════════════════════════════════════════════
 * COST MODEL
 * ═══════════════════════════════════════════════════════════════════════
 *
 * Table Scan cost  = numPages (sequential read of all pages)
 * Index Scan cost  = treeHeight ≈ 3 (for our workload sizes)
 * Filter cost      = inputRows (each row checked against predicate)
 * NLJ cost         = |outer| + |outer| × |inner|
 *
 * ═══════════════════════════════════════════════════════════════════════
 */
class Optimizer {
public:
  explicit Optimizer(Catalog &catalog);

  /**
   * Transform an AST into an optimized physical execution plan.
   * Returns a tree of PlanNodes ready for Volcano-style execution.
   */
  std::unique_ptr<PlanNode> optimize(ASTNode *ast);

  /**
   * Get a human-readable explanation of the last optimization decision.
   * Useful for demos and debugging.
   */
  const std::string &getExplanation() const { return explanation_; }

private:
  // ── Plan generation for each SQL type ───────────────────────────
  std::unique_ptr<PlanNode> optimizeSelect(SelectNode *select);

  // ── Cost estimation helpers ─────────────────────────────────────
  double estimateSelectivity(const std::string &tableName,
                             const std::string &col, CompOp op);
  double estimateTableScanCost(const std::string &tableName);
  double estimateIndexScanCost();
  bool isIndexedColumn(const std::string &col);

  Catalog &catalog_;
  std::string explanation_;
};

#endif // MINIDB_OPTIMIZER_H
