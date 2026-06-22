#include "optimizer/optimizer.h"

namespace minidb {

double Optimizer::selectivity(TableInfo *table, const Predicate &p) {
  int col = table->schema.getColIdx(p.column);
  bool unique = (col >= 0 && col == table->pk_col);
  int n = max(1, table->num_tuples);
  switch (p.op) {
    case CompOp::kEq: return unique ? 1.0 / n : 0.10;   // PK eq = 1 row
    case CompOp::kNe: return 0.90;                       // != keeps most rows
    case CompOp::kLt: case CompOp::kLe:
    case CompOp::kGt: case CompOp::kGe: return 0.33;     // a range: ~one third
  }
  return 1.0;
}

ScanChoice Optimizer::chooseScan(TableInfo *table,
                                 const vector<Predicate> &preds) {
  ScanChoice c;
  c.residual = preds;            // we always re-check predicates after fetching
  int n = max(1, table->num_tuples);

  // Combined selectivity of all predicates (assume independence -> multiply).
  double sel = 1.0;
  for (auto &p : preds) sel *= selectivity(table, p);
  c.est_rows = max(1.0, n * sel);

  double seq_cost = n;           // a sequential scan reads every tuple

  // Look for the most selective predicate on an INDEXED column.
  const Predicate *best = nullptr;
  IndexInfo *best_index = nullptr;
  double best_rows = seq_cost;
  for (auto &p : preds) {
    if (p.op == CompOp::kNe) continue;            // index can't help "!="
    int col = table->schema.getColIdx(p.column);
    if (col < 0) continue;
    IndexInfo *idx = table->indexOnColumn(col);
    if (idx == nullptr) continue;
    double rows = max(1.0, n * selectivity(table, p));
    if (rows < best_rows) { best_rows = rows; best = &p; best_index = idx; }
  }

  // Use the index only if it is cheaper than scanning everything.
  if (best != nullptr && best_rows < seq_cost) {
    c.use_index = true;
    c.index = best_index;
    c.est_cost = best_rows;
    c.est_rows = max(1.0, n * sel);
    // Translate the predicate into B+ Tree key bounds (inclusive range()).
    switch (best->op) {
      case CompOp::kEq:
        c.low  = make_unique<Value>(best->value);
        c.high = make_unique<Value>(best->value);
        break;
      case CompOp::kGt: case CompOp::kGe:
        c.low  = make_unique<Value>(best->value);   // residual filters '>'
        break;
      case CompOp::kLt: case CompOp::kLe:
        c.high = make_unique<Value>(best->value);
        break;
      default: break;
    }
    c.explain = "IndexScan on " + table->name + "." + best->column +
                " using " + best_index->name +
                " (est " + to_string((long)best_rows) + " rows)";
  } else {
    c.use_index = false;
    c.est_cost = seq_cost;
    c.explain = "SeqScan on " + table->name +
                " (est " + to_string((long)c.est_rows) + " of " +
                to_string(n) + " rows)";
  }
  return c;
}

bool Optimizer::aShouldBeOuter(TableInfo *a, const vector<Predicate> &a_preds,
                               TableInfo *b, const vector<Predicate> &b_preds) {
  // put the table producing fewer rows on the outside of the nested loop.
  double a_rows = chooseScan(a, a_preds).est_rows;
  double b_rows = chooseScan(b, b_preds).est_rows;
  return a_rows <= b_rows;
}

}  // namespace minidb
