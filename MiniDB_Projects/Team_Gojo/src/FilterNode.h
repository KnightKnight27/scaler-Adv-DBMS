#ifndef MINIDB_FILTER_NODE_H
#define MINIDB_FILTER_NODE_H

#include <memory>
#include <string>

#include "Parser.h"
#include "PlanNode.h"
#include "Record.h"
#include "Schema.h"

/**
 * FilterNode — Applies a selection predicate to its child operator.
 *
 * Implements the SQL WHERE clause for non-indexed columns.
 * Wraps any child PlanNode and filters out records that don't satisfy
 * the predicate.
 */
class FilterNode : public PlanNode {
public:
  FilterNode(std::unique_ptr<PlanNode> child,
             const std::string& col, CompOp op, Value val)
      : child_(std::move(child)), col_(col), op_(op), val_(std::move(val)),
        hasBuffered_(false) {}

  const Schema& getSchema() const override {
    return child_->getSchema();
  }

  void open() override {
    child_->open();
    hasBuffered_ = false;
  }

  bool hasNext() override {
    while (child_->hasNext()) {
      Record r = child_->next();
      if (matchesPredicate(r)) {
        buffered_    = r;
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

  void close() override {
    child_->close();
  }

  std::vector<Record> nextBatch(int batchSize = 100) override {
    std::vector<Record> result;
    result.reserve(batchSize);

    while (static_cast<int>(result.size()) < batchSize) {
      auto batch = child_->nextBatch(batchSize);
      if (batch.empty()) break;

      for (auto& r : batch) {
        if (matchesPredicate(r)) {
          result.push_back(r);
          if (static_cast<int>(result.size()) >= batchSize) break;
        }
      }
    }

    return result;
  }

private:
  bool matchesPredicate(const Record& r) const {
    const Schema& schema = child_->getSchema();
    int colIdx = schema.getColumnIndex(col_);
    if (colIdx == -1) {
      return false;
    }

    const Value& fieldVal = r.values[colIdx];

    switch (op_) {
      case CompOp::EQUALS:  return fieldVal == val_;
      case CompOp::LESS:    return fieldVal <  val_;
      case CompOp::GREATER: return fieldVal >  val_;
      default:              return true;
    }
  }

  std::unique_ptr<PlanNode> child_;
  std::string col_;
  CompOp op_;
  Value val_;
  Record buffered_;
  bool hasBuffered_;
};

#endif // MINIDB_FILTER_NODE_H
