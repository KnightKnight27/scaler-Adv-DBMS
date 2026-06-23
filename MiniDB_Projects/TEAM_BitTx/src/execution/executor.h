#pragma once

#include "catalog/table_heap.h"
#include "catalog/catalog.h"
#include "index/b_plus_tree.h"
#include "common/tuple.h"
#include "executor/evaluator.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace minidb {

using namespace std;

class ExecutorContext {
public:
  ExecutorContext() = default;
};

class Optimizer;
class CatalogManager;
class AbstractExecutor {
public:
  virtual ~AbstractExecutor() = default;
  virtual void Init() = 0;
  virtual bool Next(Tuple* tuple) = 0;
  virtual const Schema& GetOutputSchema() const = 0;

  virtual AbstractExecutor* GetChild() const {
    return nullptr;
  }
  virtual void SetChild(unique_ptr<AbstractExecutor>) {}
};

class SeqScanExecutor : public AbstractExecutor {
  friend class Optimizer;

public:
  SeqScanExecutor(ExecutorContext* ctx, TableHeap* table) : ctx_(ctx), table_(table) {}

  void Init() override;
  bool Next(Tuple* tuple) override;
  const Schema& GetOutputSchema() const override;

  TableHeap* GetTable() const {
    return table_;
  }

private:
  ExecutorContext* ctx_;
  TableHeap* table_;
  vector<RecordId> rids_;
  size_t cursor_ = 0;
};

class IndexScanExecutor : public AbstractExecutor {
  friend class Optimizer;

public:
  IndexScanExecutor(ExecutorContext* ctx, TableHeap* table, const Value& key, size_t keyIdx, CatalogManager* catalog = nullptr, const string& tableName = "")
      : ctx_(ctx), table_(table), key_(key), keyIdx_(keyIdx), catalog_(catalog), tableName_(tableName), cursor_(0) {}

  void Init() override;
  bool Next(Tuple* tuple) override;
  const Schema& GetOutputSchema() const override;

  TableHeap* GetTable() const {
    return table_;
  }

private:
  ExecutorContext* ctx_;
  TableHeap* table_;
  Value key_;
  size_t keyIdx_;
  CatalogManager* catalog_;
  string tableName_;
  vector<RecordId> rids_;
  size_t cursor_ = 0;
};

class FilterExecutor : public AbstractExecutor {
  friend class Optimizer;

public:
  FilterExecutor(unique_ptr<AbstractExecutor> child, std::function<bool(const Tuple&)> pred, unique_ptr<Expr> expr = nullptr)
      : child_(move(child)), pred_(move(pred)), expr_(move(expr)) {}

  void Init() override;
  bool Next(Tuple* tuple) override;
  const Schema& GetOutputSchema() const override;

  AbstractExecutor* GetChild() const override {
    return child_.get();
  }
  void SetChild(unique_ptr<AbstractExecutor> child) override {
    child_ = move(child);
  }

private:
  unique_ptr<AbstractExecutor> child_;
  std::function<bool(const Tuple&)> pred_;
  unique_ptr<Expr> expr_;
};

class ProjectExecutor : public AbstractExecutor {
  friend class Optimizer;

public:
  ProjectExecutor(unique_ptr<AbstractExecutor> child, vector<size_t> indices)
      : child_(move(child)), indices_(move(indices)) {}

  void Init() override;
  bool Next(Tuple* tuple) override;
  const Schema& GetOutputSchema() const override;

private:
  unique_ptr<AbstractExecutor> child_;
  vector<size_t> indices_;
};

class InsertExecutor : public AbstractExecutor {
public:
  InsertExecutor(CatalogManager* catalog, const string& tableName, TableHeap* table, vector<Value> values)
      : catalog_(catalog), tableName_(tableName), table_(table), values_(move(values)) {}

  InsertExecutor(ExecutorContext* /*ctx*/, TableHeap* table, vector<Value> values)
      : catalog_(nullptr), tableName_(""), table_(table), values_(move(values)) {}

  void Init() override {
    produced_ = false;
  }
  bool Next(Tuple* tuple) override;
  const Schema& GetOutputSchema() const override;

private:
  CatalogManager* catalog_;
  string tableName_;
  TableHeap* table_;
  vector<Value> values_;
  bool produced_ = false;
};

class DeleteExecutor : public AbstractExecutor {
public:
  DeleteExecutor(CatalogManager* catalog, const string& tableName, TableHeap* table, unique_ptr<AbstractExecutor> child)
      : catalog_(catalog), tableName_(tableName), table_(table), child_(move(child)) {}

  void Init() override {
    child_->Init();
    count_ = 0;
    produced_ = false;
  }

  bool Next(Tuple* tuple) override {
    if (produced_)
      return false;
    Tuple t;
    while (child_->Next(&t)) {
      if (t.GetRid().IsValid()) {
        if (table_->DeleteTuple(t.GetRid())) {
          count_++;
          if (catalog_) {
            const auto& schema = table_->GetSchema();
            for (size_t i = 0; i < schema.GetColumnCount(); ++i) {
              if (auto index = catalog_->GetIndex(tableName_, schema.GetColumn(i).GetName())) {
                index->Remove(t.GetValue(i));
              }
            }
          }
        }
      }
    }
    if (tuple) {
      *tuple = Tuple({Value((int32_t)count_)});
    }
    produced_ = true;
    return true;
  }

  const Schema& GetOutputSchema() const override {
    static const Schema s({Column("count", TypeId::INTEGER)});
    return s;
  }

private:
  CatalogManager* catalog_;
  string tableName_;
  TableHeap* table_;
  unique_ptr<AbstractExecutor> child_;
  int32_t count_ = 0;
  bool produced_ = false;
};

class UpdateExecutor : public AbstractExecutor {
public:
  UpdateExecutor(CatalogManager* catalog, const string& tableName, TableHeap* table, unique_ptr<AbstractExecutor> child,
                 size_t colIdx, unique_ptr<Expr> expr)
      : catalog_(catalog), tableName_(tableName), table_(table), child_(move(child)), colIdx_(colIdx), expr_(move(expr)) {}

  void Init() override {
    child_->Init();
    count_ = 0;
    produced_ = false;
  }

  bool Next(Tuple* tuple) override {
    if (produced_)
      return false;
    Tuple t;
    while (child_->Next(&t)) {
      if (t.GetRid().IsValid()) {
        Value val = Evaluator::Eval(*expr_, t.GetValues(), &table_->GetSchema());
        vector<Value> vals = t.GetValues();
        if (colIdx_ < vals.size()) {
          Value oldVal = vals[colIdx_];
          vals[colIdx_] = val;
          Tuple newTuple(vals);
          if (table_->UpdateTuple(t.GetRid(), newTuple)) {
            count_++;
            if (catalog_) {
              const auto& schema = table_->GetSchema();
              for (size_t i = 0; i < schema.GetColumnCount(); ++i) {
                if (auto index = catalog_->GetIndex(tableName_, schema.GetColumn(i).GetName())) {
                  index->Remove(t.GetValue(i));
                  index->Insert(newTuple.GetValue(i), t.GetRid());
                }
              }
            }
          }
        }
      }
    }
    if (tuple) {
      *tuple = Tuple({Value((int32_t)count_)});
    }
    produced_ = true;
    return true;
  }

  const Schema& GetOutputSchema() const override {
    static const Schema s({Column("count", TypeId::INTEGER)});
    return s;
  }

private:
  CatalogManager* catalog_;
  string tableName_;
  TableHeap* table_;
  unique_ptr<AbstractExecutor> child_;
  size_t colIdx_;
  unique_ptr<Expr> expr_;
  int32_t count_ = 0;
  bool produced_ = false;
};

class AggregateExecutor : public AbstractExecutor {
  friend class Optimizer;

public:
  enum AggType { COUNT, SUM, MIN, MAX, AVG };
  AggregateExecutor(unique_ptr<AbstractExecutor> child, AggType type, size_t colIdx)
      : child_(move(child)), type_(type), colIdx_(colIdx) {}

  void Init() override {
    child_->Init();
    count_ = 0;
    sum_ = 0;
    min_ = INT32_MAX;
    max_ = INT32_MIN;
    produced_ = false;
  }
  bool Next(Tuple* tuple) override {
    if (produced_)
      return false;
    if (type_ == AggType::COUNT) {
      Tuple t;
      while (child_->Next(&t))
        ++count_;
      *tuple = Tuple({Value((int32_t)count_)});
    } else {
      Tuple t;
      while (child_->Next(&t)) {
        if (t.GetSize() == 0)
          continue;
        Value v = t.GetValue(colIdx_);
        if (v.GetTypeId() != TypeId::INTEGER)
          continue;
        int32_t val = v.GetAsInteger();
        ++count_;
        sum_ += val;
        if (val < min_)
          min_ = val;
        if (val > max_)
          max_ = val;
      }
      if (count_ == 0)
        return false;
      Value out;
      switch (type_) {
      case AggType::SUM:
        out = Value((int64_t)sum_);
        break;
      case AggType::MIN:
        out = Value(min_);
        break;
      case AggType::MAX:
        out = Value(max_);
        break;
      case AggType::AVG:
        out = Value(sum_ / (int64_t)count_);
        break;
      default:
        return false;
      }
      *tuple = Tuple({out});
    }
    produced_ = true;
    return true;
  }
  const Schema& GetOutputSchema() const override {
    static Schema out({Column("agg", TypeId::INTEGER)});
    return out;
  }

private:
  unique_ptr<AbstractExecutor> child_;
  AggType type_;
  size_t colIdx_;
  int64_t count_ = 0;
  int64_t sum_ = 0;
  int32_t min_ = INT32_MAX;
  int32_t max_ = INT32_MIN;
  bool produced_ = false;
};

class LimitExecutor : public AbstractExecutor {
  friend class Optimizer;

public:
  LimitExecutor(unique_ptr<AbstractExecutor> child, int64_t limit)
      : child_(move(child)), limit_(limit), emitted_(0) {}

  void Init() override {
    child_->Init();
    emitted_ = 0;
  }
  bool Next(Tuple* tuple) override {
    if (emitted_ >= limit_)
      return false;
    if (!child_->Next(tuple))
      return false;
    emitted_++;
    return true;
  }
  const Schema& GetOutputSchema() const override {
    return child_->GetOutputSchema();
  }

private:
  unique_ptr<AbstractExecutor> child_;
  int64_t limit_;
  int64_t emitted_;
};

class SortExecutor : public AbstractExecutor {
  friend class Optimizer;

public:
  SortExecutor(unique_ptr<AbstractExecutor> child, size_t colIdx, bool descending)
      : child_(move(child)), colIdx_(colIdx), descending_(descending) {}

  void Init() override {
    child_->Init();
    rows_.clear();
    Tuple t;
    while (child_->Next(&t))
      rows_.push_back(t);
    std::sort(rows_.begin(), rows_.end(), [this](const Tuple& a, const Tuple& b) {
      int32_t av = a.GetValue(colIdx_).GetAsInteger();
      int32_t bv = b.GetValue(colIdx_).GetAsInteger();
      return descending_ ? av > bv : av < bv;
    });
    cursor_ = 0;
  }
  bool Next(Tuple* tuple) override {
    if (cursor_ >= rows_.size())
      return false;
    *tuple = rows_[cursor_++];
    return true;
  }
  const Schema& GetOutputSchema() const override {
    return child_->GetOutputSchema();
  }

private:
  unique_ptr<AbstractExecutor> child_;
  size_t colIdx_;
  bool descending_;
  vector<Tuple> rows_;
  size_t cursor_ = 0;
};

class DistinctExecutor : public AbstractExecutor {
  friend class Optimizer;

public:
  explicit DistinctExecutor(unique_ptr<AbstractExecutor> child) : child_(move(child)) {}

  void Init() override {
    child_->Init();
    seen_.clear();
    rows_.clear();
    Tuple t;
    while (child_->Next(&t)) {
      if (seen_.insert(KeyOf(t)).second)
        rows_.push_back(t);
    }
    cursor_ = 0;
  }
  bool Next(Tuple* tuple) override {
    if (cursor_ >= rows_.size())
      return false;
    *tuple = rows_[cursor_++];
    return true;
  }
  const Schema& GetOutputSchema() const override {
    return child_->GetOutputSchema();
  }

private:
  static int64_t KeyOf(const Tuple& t) {
    int64_t h = 1469598103934665603LL;
    for (size_t i = 0; i < t.GetSize(); ++i) {
      h ^= t.GetValue(i).GetAsInteger();
      h *= 1099511628211LL;
    }
    return h;
  }
  unique_ptr<AbstractExecutor> child_;
  unordered_set<int64_t> seen_;
  vector<Tuple> rows_;
  size_t cursor_ = 0;
};

class GroupByExecutor : public AbstractExecutor {
  friend class Optimizer;

public:
  GroupByExecutor(unique_ptr<AbstractExecutor> child, vector<size_t> keyIdx, vector<size_t> aggIdx,
                  vector<AggregateExecutor::AggType> aggTypes)
      : child_(move(child)), keyIdx_(move(keyIdx)), aggIdx_(move(aggIdx)),
        aggTypes_(move(aggTypes)) {}

  void Init() override {
    child_->Init();
    groups_.clear();
    order_.clear();
    groupRows_.clear();
    Tuple t;
    while (child_->Next(&t)) {
      int64_t h = 1469598103934665603LL;
      for (auto k : keyIdx_) {
        h ^= t.GetValue(k).GetAsInteger();
        h *= 1099511628211LL;
      }
      if (groups_.find(h) == groups_.end()) {
        order_.push_back(h);
        groupRows_[h] = vector<Tuple>();
        groups_[h] = vector<Value>(aggIdx_.size());
      }
      groupRows_[h].push_back(t);
    }
    for (auto h : order_) {
      auto& aggVals = groups_[h];
      auto& rows = groupRows_[h];
      for (size_t i = 0; i < aggIdx_.size(); ++i) {
        switch (aggTypes_[i]) {
        case AggregateExecutor::AggType::COUNT: {
          aggVals[i] = Value(static_cast<int32_t>(rows.size()));
          break;
        }
        case AggregateExecutor::AggType::SUM: {
          int64_t s = 0;
          for (auto& r : rows)
            s += r.GetValue(aggIdx_[i]).GetAsInteger();
          aggVals[i] = Value(s);
          break;
        }
        case AggregateExecutor::AggType::MIN: {
          int32_t m = INT32_MAX;
          for (auto& r : rows)
            m = min(m, r.GetValue(aggIdx_[i]).GetAsInteger());
          aggVals[i] = Value(m);
          break;
        }
        case AggregateExecutor::AggType::MAX: {
          int32_t m = INT32_MIN;
          for (auto& r : rows)
            m = max(m, r.GetValue(aggIdx_[i]).GetAsInteger());
          aggVals[i] = Value(m);
          break;
        }
        default: {
          int64_t s = 0;
          for (auto& r : rows)
            s += r.GetValue(aggIdx_[i]).GetAsInteger();
          aggVals[i] = Value(s / (int64_t)rows.size());
          break;
        }
        }
      }
    }
    cursor_ = 0;
  }
  bool Next(Tuple* tuple) override {
    if (cursor_ >= order_.size())
      return false;
    auto h = order_[cursor_++];
    vector<Value> out;
    for (auto k : keyIdx_)
      out.push_back(groupRows_[h].front().GetValue(k));
    for (auto& v : groups_[h])
      out.push_back(v);
    *tuple = Tuple(move(out));
    return true;
  }
  const Schema& GetOutputSchema() const override {
    return child_->GetOutputSchema();
  }

private:
  unique_ptr<AbstractExecutor> child_;
  vector<size_t> keyIdx_;
  vector<size_t> aggIdx_;
  vector<AggregateExecutor::AggType> aggTypes_;
  unordered_map<int64_t, vector<Value>> groups_;
  unordered_map<int64_t, vector<Tuple>> groupRows_;
  vector<int64_t> order_;
  size_t cursor_ = 0;
};

class NestedLoopJoinExecutor : public AbstractExecutor {
  friend class Optimizer;

public:
  NestedLoopJoinExecutor(ExecutorContext* ctx, unique_ptr<AbstractExecutor> left,
                         unique_ptr<AbstractExecutor> right, size_t leftKey, size_t rightKey)
      : ctx_(ctx), left_(move(left)), right_(move(right)), leftKey_(leftKey), rightKey_(rightKey),
        leftEmpty_(true), rightEmpty_(true) {}

  void Init() override {
    left_->Init();
    right_->Init();
    leftEmpty_ = true;
    rightEmpty_ = true;
  }
  bool Next(Tuple* tuple) override {
    if (leftEmpty_) {
      if (!left_->Next(&leftT_))
        return false;
      leftEmpty_ = false;
      right_->Init();
      rightEmpty_ = true;
    }
    while (true) {
      if (rightEmpty_) {
        if (!right_->Next(&rightT_)) {
          leftEmpty_ = true;
          break;
        }
        rightEmpty_ = false;
      }
      bool match =
          (leftKey_ == (size_t)-1 || rightKey_ == (size_t)-1) ||
          leftT_.GetValue(leftKey_).GetAsInteger() == rightT_.GetValue(rightKey_).GetAsInteger();
      rightEmpty_ = true;
      if (!match)
        continue;
      vector<Value> out;
      for (size_t i = 0; i < leftT_.GetSize(); ++i)
        out.push_back(leftT_.GetValue(i));
      for (size_t i = 0; i < rightT_.GetSize(); ++i)
        out.push_back(rightT_.GetValue(i));
      *tuple = Tuple(move(out));
      return true;
    }
    return false;
  }
  const Schema& GetOutputSchema() const override {
    static Schema s;
    return s;
  }

private:
  ExecutorContext* ctx_;
  unique_ptr<AbstractExecutor> left_;
  unique_ptr<AbstractExecutor> right_;
  size_t leftKey_;
  size_t rightKey_;
  Tuple leftT_;
  Tuple rightT_;
  bool leftEmpty_;
  bool rightEmpty_;
};

} // namespace minidb
