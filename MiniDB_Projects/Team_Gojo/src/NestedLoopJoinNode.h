#ifndef MINIDB_NESTED_LOOP_JOIN_NODE_H
#define MINIDB_NESTED_LOOP_JOIN_NODE_H

#include <memory>
#include <string>
#include <vector>

#include "PlanNode.h"
#include "Record.h"
#include "Schema.h"

/**
 * NestedLoopJoinNode — Implements the Nested Loop Join (NLJ) algorithm.
 *
 * ALGORITHM:
 *   for each record r in outer (left) child:
 *       for each record s in inner (right) child:
 *           if r.joinCol == s.joinCol:
 *               emit concatenated (r ++ s)
 *
 * COST: O(|outer| x |inner|) — quadratic in the worst case.
 *
 * OUTPUT FORMAT (Dynamic Schema):
 * The join outputs a concatenated record: [outer columns] ++ [inner columns].
 * The output Schema is constructed by merging both input schemas.
 */
class NestedLoopJoinNode : public PlanNode {
public:
  NestedLoopJoinNode(std::unique_ptr<PlanNode> outer,
                     std::unique_ptr<PlanNode> inner,
                     const std::string& outerCol,
                     const std::string& innerCol)
      : outer_(std::move(outer)), inner_(std::move(inner)),
        outerCol_(outerCol), innerCol_(innerCol),
        innerIdx_(0), hasOuterRecord_(false) {}

  const Schema& getSchema() const override {
    return mergedSchema_;
  }

  void open() override {
    outer_->open();
    inner_->open();

    // Build the merged output schema from both children.
    outerSchema_ = outer_->getSchema();
    innerSchema_ = inner_->getSchema();

    std::vector<Column> mergedCols;
    for (const auto& c : outerSchema_.getColumns()) mergedCols.push_back(c);
    for (const auto& c : innerSchema_.getColumns()) mergedCols.push_back(c);
    mergedSchema_ = Schema(std::move(mergedCols));

    // Materialize the entire inner table into memory.
    innerRecords_.clear();
    while (inner_->hasNext()) {
      innerRecords_.push_back(inner_->next());
    }
    inner_->close();

    // Fetch the first outer record.
    hasOuterRecord_ = outer_->hasNext();
    if (hasOuterRecord_) {
      currentOuter_ = outer_->next();
    }
    innerIdx_ = 0;
  }

  bool hasNext() override {
    while (hasOuterRecord_) {
      while (innerIdx_ < static_cast<int>(innerRecords_.size())) {
        if (matchesJoinPredicate(currentOuter_, innerRecords_[innerIdx_])) {
          return true;
        }
        innerIdx_++;
      }
      hasOuterRecord_ = outer_->hasNext();
      if (hasOuterRecord_) {
        currentOuter_ = outer_->next();
        innerIdx_ = 0;
      }
    }
    return false;
  }

  Record next() override {
    // Concatenate outer and inner record values.
    Record result;
    for (const auto& v : currentOuter_.values) result.values.push_back(v);
    for (const auto& v : innerRecords_[innerIdx_].values) result.values.push_back(v);
    innerIdx_++;
    return result;
  }

  void close() override {
    outer_->close();
    innerRecords_.clear();
  }

  std::vector<Record> nextBatch(int batchSize = 100) override {
    std::vector<Record> batch;
    batch.reserve(batchSize);
    while (static_cast<int>(batch.size()) < batchSize && hasNext()) {
      batch.push_back(next());
    }
    return batch;
  }

private:
  const Value* getField(const Record& r, const Schema& schema,
                        const std::string& col) const {
    int idx = schema.getColumnIndex(col);
    if (idx >= 0 && idx < static_cast<int>(r.values.size())) {
      return &r.values[idx];
    }
    return nullptr;
  }

  bool matchesJoinPredicate(const Record& outerRec,
                             const Record& innerRec) const {
    const Value* ov = getField(outerRec, outerSchema_, outerCol_);
    const Value* iv = getField(innerRec, innerSchema_, innerCol_);
    if (!ov || !iv) return false;
    return *ov == *iv;
  }

  std::unique_ptr<PlanNode> outer_;
  std::unique_ptr<PlanNode> inner_;
  std::string outerCol_;
  std::string innerCol_;

  Schema outerSchema_;
  Schema innerSchema_;
  Schema mergedSchema_;

  std::vector<Record> innerRecords_;
  int innerIdx_;
  Record currentOuter_;
  bool hasOuterRecord_;
};

#endif // MINIDB_NESTED_LOOP_JOIN_NODE_H
