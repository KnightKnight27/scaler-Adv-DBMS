// MiniDB - physical operators in the Volcano (iterator) model: every operator exposes
// Init() + Next(&tuple), and a tree of them streams rows one at a time. Reads pull through
// SeqScan/IndexScan; Filter/Project/NestedLoopJoin transform the stream; Insert/Delete are
// "sink" operators that mutate during Init() and report RowsAffected().
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "../catalog/catalog.h"
#include "../common/tuple.h"
#include "../parser/ast.h"
#include "evaluator.h"
#include "exec_context.h"

namespace minidb {

class Executor {
public:
    virtual ~Executor() = default;
    virtual void Init() = 0;
    virtual bool Next(Tuple* out) = 0;
    virtual const Schema& OutSchema() const = 0;
    virtual int64_t RowsAffected() const { return -1; }  // DML operators override
};

// Full table scan over the heap.
class SeqScanExecutor : public Executor {
public:
    SeqScanExecutor(TableInfo* t, ExecutionContext* ctx = nullptr,
                    LockMode read_mode = LockMode::SHARED)
        : table_(t), ctx_(ctx), read_mode_(read_mode) {}
    void Init() override;
    bool Next(Tuple* out) override;
    const Schema& OutSchema() const override { return table_->schema; }

private:
    TableInfo* table_;
    ExecutionContext* ctx_;
    LockMode read_mode_;  // SHARED for plain reads, EXCLUSIVE for SELECT ... FOR UPDATE
    std::optional<HeapFile::Iterator> it_, end_;
};

// Point lookup through a B+Tree index, then fetch each RID from the heap.
class IndexScanExecutor : public Executor {
public:
    IndexScanExecutor(TableInfo* t, IndexInfo* ix, Value key, ExecutionContext* ctx = nullptr,
                      LockMode read_mode = LockMode::SHARED)
        : table_(t), index_(ix), key_(std::move(key)), ctx_(ctx), read_mode_(read_mode) {}
    void Init() override;
    bool Next(Tuple* out) override;
    const Schema& OutSchema() const override { return table_->schema; }

private:
    TableInfo* table_;
    IndexInfo* index_;
    Value key_;
    ExecutionContext* ctx_;
    LockMode read_mode_;
    std::vector<RID> rids_;
    size_t pos_ = 0;
};

// Keep only rows satisfying a predicate.
class FilterExecutor : public Executor {
public:
    FilterExecutor(std::unique_ptr<Executor> child, const Expr* pred)
        : child_(std::move(child)), pred_(pred) {}
    void Init() override { child_->Init(); }
    bool Next(Tuple* out) override;
    const Schema& OutSchema() const override { return child_->OutSchema(); }

private:
    std::unique_ptr<Executor> child_;
    const Expr* pred_;
};

// Project a subset (and order) of columns.
class ProjectExecutor : public Executor {
public:
    ProjectExecutor(std::unique_ptr<Executor> child, std::vector<int> cols, Schema out)
        : child_(std::move(child)), cols_(std::move(cols)), out_(std::move(out)) {}
    void Init() override { child_->Init(); }
    bool Next(Tuple* out) override;
    const Schema& OutSchema() const override { return out_; }

private:
    std::unique_ptr<Executor> child_;
    std::vector<int> cols_;
    Schema out_;
};

// Inner equi-join, classic nested loop. Right side is materialized at Init().
class NestedLoopJoinExecutor : public Executor {
public:
    NestedLoopJoinExecutor(std::unique_ptr<Executor> left, std::unique_ptr<Executor> right,
                           int left_key, int right_key, Schema out)
        : left_(std::move(left)), right_(std::move(right)),
          left_key_(left_key), right_key_(right_key), out_(std::move(out)) {}
    void Init() override;
    bool Next(Tuple* out) override;
    const Schema& OutSchema() const override { return out_; }

private:
    std::unique_ptr<Executor> left_, right_;
    int left_key_, right_key_;
    Schema out_;
    std::vector<Tuple> right_rows_;
    std::optional<Tuple> cur_left_;
    size_t right_pos_ = 0;
};

// Insert rows into a table and maintain its indexes.
class InsertExecutor : public Executor {
public:
    InsertExecutor(TableInfo* t, std::vector<std::vector<Value>> rows, ExecutionContext* ctx = nullptr)
        : table_(t), rows_(std::move(rows)), ctx_(ctx) {}
    void Init() override;
    bool Next(Tuple*) override { return false; }
    const Schema& OutSchema() const override { return table_->schema; }
    int64_t RowsAffected() const override { return affected_; }

private:
    TableInfo* table_;
    std::vector<std::vector<Value>> rows_;
    ExecutionContext* ctx_;
    int64_t affected_ = 0;
};

// Delete rows produced by a child scan and maintain indexes.
class DeleteExecutor : public Executor {
public:
    DeleteExecutor(TableInfo* t, std::unique_ptr<Executor> child, ExecutionContext* ctx = nullptr)
        : table_(t), child_(std::move(child)), ctx_(ctx) {}
    void Init() override;
    bool Next(Tuple*) override { return false; }
    const Schema& OutSchema() const override { return table_->schema; }
    int64_t RowsAffected() const override { return affected_; }

private:
    TableInfo* table_;
    std::unique_ptr<Executor> child_;
    ExecutionContext* ctx_;
    int64_t affected_ = 0;
};

// Coerce a parsed literal to a target column type.
Value CoerceTo(const Value& v, TypeId target);

}  // namespace minidb
