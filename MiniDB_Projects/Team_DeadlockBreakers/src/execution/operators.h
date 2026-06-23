#pragma once

#include "common/types.h"
#include "execution/ast.h"
#include "execution/batch.h"
#include "catalog/catalog.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"

#include <functional>
#include <memory>
#include <vector>

namespace minidb {

bool CompareValues(const Value& lhs, const Value& rhs, CmpOp op);

/*
 * Operator base class — Volcano-style iterator with batch output (Track A).
 * Each call to Next() returns a TupleBatch instead of a single row.
 */
class Operator {
public:
    virtual ~Operator() = default;
    virtual void Open() = 0;
    virtual TupleBatch Next() = 0;   // returns empty batch when done
    virtual void Close() = 0;
    virtual Schema GetOutputSchema() const = 0;
};

/*
 * SeqScanOperator: scans an entire heap file, returns batches of rows.
 */
class SeqScanOperator : public Operator {
public:
    SeqScanOperator(TableInfo* table);
    void Open() override;
    TupleBatch Next() override;
    void Close() override;
    Schema GetOutputSchema() const override;

private:
    TableInfo*  table_;
    std::vector<Row> all_rows_;
    size_t cursor_ = 0;
};

/*
 * IndexScanOperator: uses B+ tree to look up rows by primary key.
 */
class IndexScanOperator : public Operator {
public:
    IndexScanOperator(TableInfo* table, int64_t key);
    IndexScanOperator(TableInfo* table, int64_t lo, int64_t hi); // range
    void Open() override;
    TupleBatch Next() override;
    void Close() override;
    Schema GetOutputSchema() const override;

private:
    TableInfo*  table_;
    bool        is_range_ = false;
    int64_t     key_lo_;
    int64_t     key_hi_;
    std::vector<Row> result_rows_;
    bool        done_ = false;
};

/*
 * FilterOperator: applies predicates to incoming batches.
 */
class FilterOperator : public Operator {
public:
    FilterOperator(std::unique_ptr<Operator> child,
                   const WhereClause& where_clause,
                   const Schema& schema);
    void Open() override;
    TupleBatch Next() override;
    void Close() override;
    Schema GetOutputSchema() const override;

    // Evaluate a predicate against a row
    static bool EvalPredicate(const Predicate& pred, const Row& row,
                              const Schema& schema);
    
    // Evaluate a where clause against a row
    static bool EvalWhereClause(const WhereClause& clause, const Row& row, const Schema& schema);

private:
    std::unique_ptr<Operator>  child_;
    WhereClause                where_clause_;
    Schema                     schema_;
};

/*
 * NestedLoopJoinOperator: batch-aware nested loop join.
 */
class NestedLoopJoinOperator : public Operator {
public:
    NestedLoopJoinOperator(std::unique_ptr<Operator> left,
                           std::unique_ptr<Operator> right,
                           const JoinCondition& cond,
                           const Schema& left_schema,
                           const Schema& right_schema);
    void Open() override;
    TupleBatch Next() override;
    void Close() override;
    Schema GetOutputSchema() const override;

private:
    std::unique_ptr<Operator> left_;
    std::unique_ptr<Operator> right_;
    JoinCondition             cond_;
    Schema                    left_schema_;
    Schema                    right_schema_;
    Schema                    output_schema_;

    // Materialized right side for nested loop
    std::vector<Row>          right_rows_;
    TupleBatch                left_batch_;
    size_t                    left_idx_ = 0;
    size_t                    right_idx_ = 0;
    bool                      left_exhausted_ = false;

    int FindCol(const Schema& schema, const std::string& col_name) const;
};

/*
 * ProjectionOperator: selects specific columns from batches.
 */
class ProjectionOperator : public Operator {
public:
    ProjectionOperator(std::unique_ptr<Operator> child,
                       const std::vector<std::string>& columns,
                       const Schema& input_schema);
    void Open() override;
    TupleBatch Next() override;
    void Close() override;
    Schema GetOutputSchema() const override;

private:
    std::unique_ptr<Operator>   child_;
    std::vector<std::string>    columns_;
    Schema                      input_schema_;
    Schema                      output_schema_;
    std::vector<int>            col_indices_;
};

// ── Row-at-a-time operators (for benchmark comparison) ──────

/*
 * RowOperator: processes one row at a time (traditional Volcano).
 */
class RowOperator {
public:
    virtual ~RowOperator() = default;
    virtual void Open() = 0;
    virtual bool Next(Row& out) = 0;  // returns false when done
    virtual void Close() = 0;
    virtual Schema GetOutputSchema() const = 0;
};

class SeqScanRowOperator : public RowOperator {
public:
    SeqScanRowOperator(TableInfo* table);
    void Open() override;
    bool Next(Row& out) override;
    void Close() override;
    Schema GetOutputSchema() const override;
private:
    TableInfo* table_;
    std::vector<Row> all_rows_;
    size_t cursor_ = 0;
};

class FilterRowOperator : public RowOperator {
public:
    FilterRowOperator(std::unique_ptr<RowOperator> child,
                      const WhereClause& where_clause,
                      const Schema& schema);
    void Open() override;
    bool Next(Row& out) override;
    void Close() override;
    Schema GetOutputSchema() const override;
private:
    std::unique_ptr<RowOperator> child_;
    WhereClause                  where_clause_;
    Schema                       schema_;
};

} // namespace minidb
