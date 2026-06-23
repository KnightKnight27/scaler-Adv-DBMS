#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "catalog/schema.h"
#include "engine/storage_engine.h"
#include "execution/iterator.h"
#include "parser/ast.h"

namespace minidb {

// Full table scan: pulls every row from the engine's heap scan and decodes it.
class SeqScan : public Operator {
public:
    SeqScan(StorageEngine* engine, std::string table, Schema schema, std::string alias);
    void open() override;
    bool next(Tuple& out) override;
    void close() override {}
    const OutSchema& out_schema() const override { return out_schema_; }
private:
    StorageEngine*                          engine_;
    std::string                             table_;
    Schema                                  schema_;
    OutSchema                               out_schema_;
    std::unique_ptr<StorageEngine::Cursor>  cursor_;
};

// Index scan: a bounded key range via the engine's index (range/point lookup).
class IndexScan : public Operator {
public:
    IndexScan(StorageEngine* engine, std::string table, Schema schema, std::string alias,
              std::int64_t lo, std::int64_t hi);
    void open() override;
    bool next(Tuple& out) override;
    void close() override {}
    const OutSchema& out_schema() const override { return out_schema_; }
private:
    StorageEngine*                          engine_;
    std::string                             table_;
    Schema                                  schema_;
    OutSchema                               out_schema_;
    std::int64_t                            lo_, hi_;
    std::unique_ptr<StorageEngine::Cursor>  cursor_;
};

// Filter: passes through child tuples that satisfy a predicate.
class Filter : public Operator {
public:
    Filter(std::unique_ptr<Operator> child, const Expr* predicate)
        : child_(std::move(child)), predicate_(predicate) {}
    void open() override { child_->open(); }
    bool next(Tuple& out) override;
    void close() override { child_->close(); }
    const OutSchema& out_schema() const override { return child_->out_schema(); }
private:
    std::unique_ptr<Operator> child_;
    const Expr*               predicate_;
};

// Project: keeps a chosen subset of columns (by index into the child schema).
class Project : public Operator {
public:
    Project(std::unique_ptr<Operator> child, std::vector<int> indices, OutSchema out);
    void open() override { child_->open(); }
    bool next(Tuple& out) override;
    void close() override { child_->close(); }
    const OutSchema& out_schema() const override { return out_schema_; }
private:
    std::unique_ptr<Operator> child_;
    std::vector<int>          indices_;
    OutSchema                 out_schema_;
};

// Nested-loop join: materialises the right side once, then for each left tuple
// scans it applying the ON predicate. Works for any join predicate.
class NestedLoopJoin : public Operator {
public:
    NestedLoopJoin(std::unique_ptr<Operator> left, std::unique_ptr<Operator> right,
                   const Expr* on);
    void open() override;
    bool next(Tuple& out) override;
    void close() override;
    const OutSchema& out_schema() const override { return out_schema_; }
private:
    std::unique_ptr<Operator> left_, right_;
    const Expr*               on_;
    OutSchema                 out_schema_;
    std::vector<Tuple>        right_rows_;
    Tuple                     cur_left_;
    bool                      have_left_ = false;
    std::size_t               right_idx_ = 0;
};

// Hash join (equi-join on left_key = right_key). Builds a hash table on one side
// (the optimizer picks the smaller via build_on_left) and probes with the other;
// output is always in left++right column order regardless of build side.
// O(n+m) vs nested loop's O(n*m).
class HashJoin : public Operator {
public:
    HashJoin(std::unique_ptr<Operator> left, std::unique_ptr<Operator> right,
             int left_key, int right_key, bool build_on_left);
    void open() override;
    bool next(Tuple& out) override;
    void close() override;
    const OutSchema& out_schema() const override { return out_schema_; }
private:
    std::unique_ptr<Operator>                          left_, right_;
    int                                                left_key_, right_key_;
    bool                                               build_on_left_;
    OutSchema                                          out_schema_;
    std::unordered_map<std::string, std::vector<Tuple>> build_;  // keyed by build-side key
    Tuple                                              cur_probe_;
    bool                                               have_probe_ = false;
    const std::vector<Tuple>*                          bucket_ = nullptr;
    std::size_t                                        bucket_idx_ = 0;
};

// Aggregate with GROUP BY: COUNT/SUM/AVG/MIN/MAX. Materialises groups on open().
struct AggSpec {
    std::string func;  // COUNT, SUM, AVG, MIN, MAX
    int         col;   // child column index, or -1 for COUNT(*)
};
class Aggregate : public Operator {
public:
    Aggregate(std::unique_ptr<Operator> child, std::vector<int> group_by,
              std::vector<AggSpec> aggs, OutSchema out);
    void open() override;
    bool next(Tuple& out) override;
    void close() override { child_->close(); }
    const OutSchema& out_schema() const override { return out_schema_; }
private:
    std::unique_ptr<Operator> child_;
    std::vector<int>          group_by_;
    std::vector<AggSpec>      aggs_;
    OutSchema                 out_schema_;
    std::vector<Tuple>        results_;
    std::size_t               pos_ = 0;
};

} // namespace minidb
