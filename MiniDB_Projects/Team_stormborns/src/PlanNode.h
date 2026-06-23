#ifndef MINIDB_PLAN_NODE_H
#define MINIDB_PLAN_NODE_H

#include <vector>

#include "Record.h"

/**
 * PlanNode is the abstract interface for the Volcano iterator model.
 *
 * ═══════════════════════════════════════════════════════════════════════
 * THE VOLCANO MODEL (Goetz Graefe, 1994)
 * ═══════════════════════════════════════════════════════════════════════
 *
 * Every relational operator (scan, filter, join, etc.) is implemented as
 * an iterator with three methods: open(), hasNext()/next(), close().
 *
 * Execution is DEMAND-DRIVEN (pull-based):
 *   - The top-level consumer calls next() on the root operator
 *   - The root operator calls next() on its children
 *   - This propagates all the way down to the leaf (scan) operators
 *   - Records flow UP one at a time through the operator tree
 *
 * ADVANTAGES:
 *   1. Memory-efficient: only one record per operator is in memory
 *   2. Pipelined: no need to materialize intermediate results
 *   3. Composable: operators can be freely combined into any tree
 *   4. Simple: each operator is a self-contained iterator
 *
 * DISADVANTAGE (motivating Track A):
 *   Per-row virtual function call overhead. Every next() call crosses
 *   a virtual dispatch boundary. For millions of rows, this overhead
 *   is significant. The nextBatch() extension amortizes this cost.
 *
 * ═══════════════════════════════════════════════════════════════════════
 * TRACK A: BATCH (VECTORIZED) EXECUTION EXTENSION
 * ═══════════════════════════════════════════════════════════════════════
 *
 * The nextBatch() method returns a vector of up to `batchSize` records
 * in a single call, amortizing:
 *   1. Virtual dispatch overhead (1 call per batch instead of per row)
 *   2. Function call overhead (loop is inside the operator, not outside)
 *   3. Branch prediction misses (tight inner loop is more predictable)
 *
 * This is a simplified version of the vectorized execution model used
 * by modern analytical databases (MonetDB/X100, DuckDB, Velox).
 *
 * The default implementation calls next() in a loop, but leaf operators
 * (TableScanNode) override it with an optimized batch read.
 */
class PlanNode {
public:
  /** Initialize the operator and its children. */
  virtual void open() = 0;

  /** Returns true if there are more records to produce. */
  virtual bool hasNext() = 0;

  /** Returns the next record. Only valid if hasNext() returned true. */
  virtual Record next() = 0;

  /** Release resources. */
  virtual void close() = 0;

  /**
   * TRACK A EXTENSION: Returns a batch of up to batchSize records.
   *
   * Default implementation: calls hasNext()/next() in a loop.
   * Optimized operators override this for better throughput.
   *
   * WHY BATCHING IMPROVES THROUGHPUT:
   * - Reduces virtual function call overhead by factor of batchSize
   * - Enables CPU vectorization of tight loops
   * - Improves cache locality (records are contiguous in the vector)
   * - Amortizes iterator state management cost
   */
  virtual std::vector<Record> nextBatch(int batchSize = 100) {
    std::vector<Record> batch;
    batch.reserve(batchSize);
    for (int i = 0; i < batchSize && hasNext(); i++) {
      batch.push_back(next());
    }
    return batch;
  }

  virtual ~PlanNode() = default;
};

#endif // MINIDB_PLAN_NODE_H
