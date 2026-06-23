#ifndef MINIDB_NESTED_LOOP_JOIN_NODE_H
#define MINIDB_NESTED_LOOP_JOIN_NODE_H

#include <memory>
#include <string>
#include <vector>

#include "PlanNode.h"

/**
 * NestedLoopJoinNode — Implements the Nested Loop Join (NLJ) algorithm.
 *
 * ═══════════════════════════════════════════════════════════════════════
 * ALGORITHM:
 *   for each record r in outer (left) child:
 *       for each record s in inner (right) child:
 *           if r.joinCol == s.joinCol:
 *               emit (r, s)    ← we emit r, but with match found
 *
 * COST: O(|outer| × |inner|) — quadratic in the worst case.
 *
 * This is the simplest join algorithm. More efficient alternatives:
 *   - Sort-Merge Join: O(|outer| log |outer| + |inner| log |inner|)
 *   - Hash Join: O(|outer| + |inner|) but requires memory for hash table
 *
 * We implement NLJ because:
 *   1. It's the most straightforward to implement correctly
 *   2. It works for any join predicate (not just equi-joins)
 *   3. The optimizer mitigates cost by choosing join ORDER (smaller outer)
 *
 * JOIN ORDER OPTIMIZATION:
 * The cost |outer| × |inner| means we want the smaller table on the
 * outer side. If |T1| = 100 and |T2| = 10000:
 *   - T1 outer: 100 + 100 × 10000 = 1,000,100 cost
 *   - T2 outer: 10000 + 10000 × 100 = 1,010,000 cost
 * The optimizer picks T1 as outer because fewer outer iterations means
 * fewer full scans of the inner table.
 *
 * MATERIALIZATION TRADE-OFF:
 * We materialize the inner table into memory on open() so we can
 * "rewind" it for each outer record. Alternative: re-scan from disk
 * for each outer record (saves memory, but much slower). For our
 * workload sizes, materialization is the right choice.
 * ═══════════════════════════════════════════════════════════════════════
 *
 * OUTPUT FORMAT:
 * Since our Record has a fixed {id, val} schema, we can't really
 * concatenate two records. Instead, we output the OUTER record for
 * each matching pair. In a production database, the join would produce
 * a wider tuple combining columns from both tables.
 */
class NestedLoopJoinNode : public PlanNode {
public:
  /**
   * @param outer     Left child (should be the smaller table)
   * @param inner     Right child
   * @param outerCol  Column from outer table to join on ("id" or "val")
   * @param innerCol  Column from inner table to join on ("id" or "val")
   */
  NestedLoopJoinNode(std::unique_ptr<PlanNode> outer,
                     std::unique_ptr<PlanNode> inner,
                     const std::string &outerCol, const std::string &innerCol)
      : outer_(std::move(outer)), inner_(std::move(inner)), outerCol_(outerCol),
        innerCol_(innerCol), innerIdx_(0), hasOuterRecord_(false) {}

  void open() override {
    outer_->open();
    inner_->open();

    // Materialize the entire inner table into memory.
    // This allows us to "rewind" the inner table for each outer record
    // without re-reading from disk each time.
    innerRecords_.clear();
    while (inner_->hasNext()) {
      innerRecords_.push_back(inner_->next());
    }
    inner_->close();

    // Fetch the first outer record
    hasOuterRecord_ = outer_->hasNext();
    if (hasOuterRecord_) {
      currentOuter_ = outer_->next();
    }
    innerIdx_ = 0;
  }

  bool hasNext() override {
    // Find the next matching pair
    while (hasOuterRecord_) {
      while (innerIdx_ < static_cast<int>(innerRecords_.size())) {
        if (matchesJoinPredicate(currentOuter_, innerRecords_[innerIdx_])) {
          return true; // found a match
        }
        innerIdx_++;
      }

      // Inner exhausted for this outer — advance outer
      hasOuterRecord_ = outer_->hasNext();
      if (hasOuterRecord_) {
        currentOuter_ = outer_->next();
        innerIdx_ = 0; // rewind inner
      }
    }

    return false;
  }

  Record next() override {
    // Return a combined record.
    // Since our schema is fixed {id, val}, we output a record where:
    //   id = outer.id, val = inner.val
    // This gives us a "joined" view combining data from both tables.
    Record result;
    result.id = currentOuter_.id;
    result.val = innerRecords_[innerIdx_].val;
    innerIdx_++;
    return result;
  }

  void close() override {
    outer_->close();
    innerRecords_.clear();
  }

  /**
   * TRACK A: Batch join.
   *
   * Accumulates matching pairs into a batch vector instead of
   * returning one at a time. This reduces the number of virtual
   * function calls from the consumer.
   */
  std::vector<Record> nextBatch(int batchSize = 100) override {
    std::vector<Record> batch;
    batch.reserve(batchSize);

    while (static_cast<int>(batch.size()) < batchSize && hasNext()) {
      batch.push_back(next());
    }

    return batch;
  }

private:
  int32_t getField(const Record &r, const std::string &col) const {
    if (col == "id")
      return r.id;
    return r.val;
  }

  bool matchesJoinPredicate(const Record &outer, const Record &inner) const {
    return getField(outer, outerCol_) == getField(inner, innerCol_);
  }

  std::unique_ptr<PlanNode> outer_;
  std::unique_ptr<PlanNode> inner_;
  std::string outerCol_;
  std::string innerCol_;

  std::vector<Record> innerRecords_; // materialized inner table
  int innerIdx_;
  Record currentOuter_;
  bool hasOuterRecord_;
};

#endif // MINIDB_NESTED_LOOP_JOIN_NODE_H
