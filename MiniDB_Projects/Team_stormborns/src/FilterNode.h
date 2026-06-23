#ifndef MINIDB_FILTER_NODE_H
#define MINIDB_FILTER_NODE_H

#include <memory>
#include <string>

#include "Parser.h"
#include "PlanNode.h"

/**
 * FilterNode — Applies a selection predicate to its child operator.
 *
 * Implements the SQL WHERE clause for non-indexed columns.
 * Wraps any child PlanNode and filters out records that don't satisfy
 * the predicate.
 *
 * VOLCANO SEMANTICS:
 * On each hasNext() call, the filter pulls records from its child
 * until it finds one that matches the predicate (or the child is
 * exhausted). This is the "demand-driven" nature of Volcano — the
 * filter only pulls as many records as needed.
 *
 * PREDICATE PUSHDOWN:
 * In a production optimizer, filters are "pushed down" as close to
 * the data source as possible to reduce the number of records flowing
 * through the pipeline. Our optimizer does this implicitly — when the
 * predicate is on an indexed column, we use IndexScanNode instead of
 * TableScan + FilterNode.
 */
class FilterNode : public PlanNode {
public:
  /**
   * @param child    The child operator to filter
   * @param col      Column name to filter on ("id" or "val")
   * @param op       Comparison operator
   * @param val      Value to compare against
   */
  FilterNode(std::unique_ptr<PlanNode> child, const std::string &col, CompOp op,
             int32_t val)
      : child_(std::move(child)), col_(col), op_(op), val_(val),
        hasBuffered_(false) {}

  void open() override {
    child_->open();
    hasBuffered_ = false;
  }

  bool hasNext() override {
    // Pull from child until we find a matching record
    while (child_->hasNext()) {
      Record r = child_->next();
      if (matchesPredicate(r)) {
        buffered_ = r;
        hasBuffered_ = true;
        return true;
      }
    }
    hasBuffered_ = false;
    return false;
  }

  Record next() override {
    hasBuffered_ = false;
    return buffered_;
  }

  void close() override { child_->close(); }

  /**
   * TRACK A: Batch filter.
   *
   * Pulls a batch from the child, then filters in a tight loop.
   * This is faster than per-row virtual dispatch because:
   *   1. One virtual call to child->nextBatch() instead of N calls to next()
   *   2. The filter loop is a simple branch-predictor-friendly scan
   *   3. Output vector is contiguous in memory (cache-friendly)
   */
  std::vector<Record> nextBatch(int batchSize = 100) override {
    std::vector<Record> result;
    result.reserve(batchSize);

    while (static_cast<int>(result.size()) < batchSize) {
      auto batch = child_->nextBatch(batchSize);
      if (batch.empty())
        break;

      for (auto &r : batch) {
        if (matchesPredicate(r)) {
          result.push_back(r);
          if (static_cast<int>(result.size()) >= batchSize)
            break;
        }
      }
    }

    return result;
  }

private:
  bool matchesPredicate(const Record &r) const {
    // Determine which field to compare
    int32_t fieldVal;
    if (col_ == "id") {
      fieldVal = r.id;
    } else if (col_ == "val") {
      fieldVal = r.val;
    } else {
      return false; // unknown column — no match
    }

    switch (op_) {
    case CompOp::EQUALS:
      return fieldVal == val_;
    case CompOp::LESS:
      return fieldVal < val_;
    case CompOp::GREATER:
      return fieldVal > val_;
    default:
      return true;
    }
  }

  std::unique_ptr<PlanNode> child_;
  std::string col_;
  CompOp op_;
  int32_t val_;
  Record buffered_;
  bool hasBuffered_;
};

#endif // MINIDB_FILTER_NODE_H
