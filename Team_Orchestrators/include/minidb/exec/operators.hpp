#pragma once
// Volcano-model physical operators: open() / next() / close().
// v1 plan shape for SELECT: SeqScan -> [Filter] -> [Project].
#include "minidb/exec/predicate.hpp"
#include "minidb/optional.hpp"
#include "minidb/schema.hpp"
#include "minidb/sql/ast.hpp"
#include "minidb/storage/storage_engine.hpp"
#include "minidb/types.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

class Operator {
 public:
  virtual ~Operator() = default;
  virtual void open() = 0;
  virtual Optional<Tuple> next() = 0;
  virtual void close() = 0;
  virtual const Schema& out_schema() const = 0;
};

// Full table scan via the storage engine's record iterator.
class SeqScanOp : public Operator {
 public:
  SeqScanOp(StorageEngine& engine, TableId table, Schema schema)
      : engine_(engine), table_(table), schema_(std::move(schema)) {}
  void open() override { iter_ = engine_.scan(table_); }
  Optional<Tuple> next() override {
    RID rid;
    Tuple t;
    if (iter_ && iter_->next(rid, t)) return t;
    return Optional<Tuple>();
  }
  void close() override { iter_.reset(); }
  const Schema& out_schema() const override { return schema_; }

 private:
  StorageEngine& engine_;
  TableId table_;
  Schema schema_;
  std::unique_ptr<RecordIterator> iter_;
};

// Fetches rows for an exact key via an index on a column, then materializes
// each by RID. Falls back to emitting nothing if no index exists (the planner
// only selects this when an index is present).
class IndexScanOp : public Operator {
 public:
  IndexScanOp(StorageEngine& engine, TableId table, size_t column, Value key, Schema schema)
      : engine_(engine), table_(table), column_(column),
        key_(std::move(key)), schema_(std::move(schema)) {}
  void open() override {
    rids_ = engine_.index_lookup(table_, column_, key_);
    pos_ = 0;
  }
  Optional<Tuple> next() override {
    while (pos_ < rids_.size()) {
      Optional<Tuple> t = engine_.get(table_, rids_[pos_++]);
      if (t) return t;
    }
    return Optional<Tuple>();
  }
  void close() override { rids_.clear(); pos_ = 0; }
  const Schema& out_schema() const override { return schema_; }

 private:
  StorageEngine& engine_;
  TableId table_;
  size_t column_;
  Value key_;
  Schema schema_;
  std::vector<RID> rids_;
  size_t pos_ = 0;
};

// Passes through only tuples satisfying the WHERE conjunction.
class FilterOp : public Operator {
 public:
  FilterOp(std::unique_ptr<Operator> child, WhereClause where)
      : child_(std::move(child)), where_(std::move(where)) {}
  void open() override { child_->open(); }
  Optional<Tuple> next() override {
    while (true) {
      Optional<Tuple> t = child_->next();
      if (!t) return Optional<Tuple>();
      if (eval_where(child_->out_schema(), *t, where_)) return t;
    }
  }
  void close() override { child_->close(); }
  const Schema& out_schema() const override { return child_->out_schema(); }

 private:
  std::unique_ptr<Operator> child_;
  WhereClause where_;
};

// Projects a subset of columns (by precomputed indices).
class ProjectOp : public Operator {
 public:
  ProjectOp(std::unique_ptr<Operator> child, std::vector<size_t> indices, Schema out)
      : child_(std::move(child)), indices_(std::move(indices)), out_(std::move(out)) {}
  void open() override { child_->open(); }
  Optional<Tuple> next() override {
    Optional<Tuple> t = child_->next();
    if (!t) return Optional<Tuple>();
    Tuple row;
    row.reserve(indices_.size());
    for (size_t i : indices_) row.push_back((*t)[i]);
    return Optional<Tuple>(std::move(row));
  }
  void close() override { child_->close(); }
  const Schema& out_schema() const override { return out_; }

 private:
  std::unique_ptr<Operator> child_;
  std::vector<size_t> indices_;
  Schema out_;
};

// Inner equi-join by repeated rescans of the right child (general; works for
// any right input that re-opens cleanly, e.g. SeqScanOp). Emits left columns
// followed by right columns.
class NestedLoopJoinOp : public Operator {
 public:
  NestedLoopJoinOp(std::unique_ptr<Operator> left, std::unique_ptr<Operator> right,
                   size_t left_key, size_t right_key, Schema out)
      : left_(std::move(left)), right_(std::move(right)),
        lk_(left_key), rk_(right_key), out_(std::move(out)) {}
  void open() override {
    left_->open();
    left_cur_ = left_->next();
    if (left_cur_) right_->open();
  }
  Optional<Tuple> next() override {
    while (left_cur_) {
      Optional<Tuple> r = right_->next();
      if (!r) {
        right_->close();
        left_cur_ = left_->next();
        if (left_cur_) right_->open();
        continue;
      }
      if (compare_coerced((*left_cur_)[lk_], (*r)[rk_]) == 0) {
        Tuple row = *left_cur_;
        row.insert(row.end(), r->begin(), r->end());
        return Optional<Tuple>(std::move(row));
      }
    }
    return Optional<Tuple>();
  }
  void close() override { left_->close(); }
  const Schema& out_schema() const override { return out_; }

 private:
  std::unique_ptr<Operator> left_, right_;
  size_t lk_, rk_;
  Schema out_;
  Optional<Tuple> left_cur_;
};

// Inner equi-join. Builds an in-memory hash table on the right child keyed by
// the join column, then probes with the left. Emits left columns then right.
class HashJoinOp : public Operator {
 public:
  HashJoinOp(std::unique_ptr<Operator> left, std::unique_ptr<Operator> right,
             size_t left_key, size_t right_key, Schema out)
      : left_(std::move(left)), right_(std::move(right)),
        lk_(left_key), rk_(right_key), out_(std::move(out)) {}
  void open() override {
    right_->open();
    while (Optional<Tuple> t = right_->next()) {
      std::string key = (*t)[rk_].to_string();
      build_[key].push_back(std::move(*t));
    }
    right_->close();
    left_->open();
    have_probe_ = false;
    match_idx_ = 0;
    cur_bucket_ = nullptr;
  }
  Optional<Tuple> next() override {
    while (true) {
      if (have_probe_ && cur_bucket_ && match_idx_ < cur_bucket_->size()) {
        const Tuple& r = (*cur_bucket_)[match_idx_++];
        Tuple row = probe_;
        row.insert(row.end(), r.begin(), r.end());
        return Optional<Tuple>(std::move(row));
      }
      Optional<Tuple> t = left_->next();
      if (!t) return Optional<Tuple>();
      auto it = build_.find((*t)[lk_].to_string());
      if (it == build_.end()) { have_probe_ = false; continue; }
      probe_ = std::move(*t);
      cur_bucket_ = &it->second;
      match_idx_ = 0;
      have_probe_ = true;
    }
  }
  void close() override {
    left_->close();
    build_.clear();
    cur_bucket_ = nullptr;
  }
  const Schema& out_schema() const override { return out_; }

 private:
  std::unique_ptr<Operator> left_, right_;
  size_t lk_, rk_;
  Schema out_;
  std::unordered_map<std::string, std::vector<Tuple>> build_;
  Tuple probe_;
  const std::vector<Tuple>* cur_bucket_ = nullptr;
  size_t match_idx_ = 0;
  bool have_probe_ = false;
};

// Materializes its child and emits rows sorted ascending by the given output
// column indices (lexicographic across multiple keys).
class SortOp : public Operator {
 public:
  SortOp(std::unique_ptr<Operator> child, std::vector<size_t> keys)
      : child_(std::move(child)), keys_(std::move(keys)) {}
  void open() override {
    child_->open();
    while (Optional<Tuple> t = child_->next()) buf_.push_back(std::move(*t));
    const std::vector<size_t>& keys = keys_;
    std::sort(buf_.begin(), buf_.end(), [&keys](const Tuple& a, const Tuple& b) {
      for (size_t k : keys) {
        int c = a[k].compare(b[k]);
        if (c != 0) return c < 0;
      }
      return false;
    });
    pos_ = 0;
  }
  Optional<Tuple> next() override {
    if (pos_ >= buf_.size()) return Optional<Tuple>();
    return Optional<Tuple>(std::move(buf_[pos_++]));
  }
  void close() override {
    child_->close();
    buf_.clear();
    pos_ = 0;
  }
  const Schema& out_schema() const override { return child_->out_schema(); }

 private:
  std::unique_ptr<Operator> child_;
  std::vector<size_t> keys_;
  std::vector<Tuple> buf_;
  size_t pos_ = 0;
};

}  // namespace minidb
