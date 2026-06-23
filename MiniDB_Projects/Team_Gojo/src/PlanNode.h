#ifndef MINIDB_PLAN_NODE_H
#define MINIDB_PLAN_NODE_H

#include <vector>

#include "Record.h"
#include "Schema.h"

/**
 * PlanNode is the abstract interface for the Volcano iterator model.
 *
 * Every relational operator (scan, filter, join, etc.) is implemented as
 * an iterator with three methods: open(), hasNext()/next(), close().
 *
 * Execution is DEMAND-DRIVEN (pull-based):
 *   - The top-level consumer calls next() on the root operator
 *   - The root operator calls next() on its children
 *   - This propagates all the way down to the leaf (scan) operators
 *   - Records flow UP one at a time through the operator tree
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
   * Returns the output schema of this operator.
   * Used by FilterNode and other operators to resolve column names to indices.
   */
  virtual const Schema& getSchema() const = 0;

  /**
   * TRACK A EXTENSION: Returns a batch of up to batchSize records.
   * Default implementation: calls hasNext()/next() in a loop.
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
