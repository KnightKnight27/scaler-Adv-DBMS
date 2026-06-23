// Volcano-style (pull-based) execution engine.
//
// Every physical operator implements open()/next()/close(); next() returns one
// output row at a time, so operators compose into a pipeline. Each operator
// exposes an `out_schema` describing its output columns (with table qualifiers),
// which is how WHERE/JOIN predicates resolve column references.
//
// Operators here:
//   SeqScan        -- full heap-file scan
//   IndexScan      -- primary-key B+ tree lookup / range scan
//   Filter         -- evaluate a predicate
//   NestedLoopJoin -- left-deep join with an optional ON/WHERE predicate
//   Projection     -- choose / reorder output columns
//   Aggregate      -- COUNT/SUM/MIN/MAX/AVG with optional GROUP BY
//   Sort           -- ORDER BY
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "catalog/catalog.hpp"
#include "catalog/tuple.hpp"
#include "sql/ast.hpp"
#include "storage/heap_file.hpp"

namespace minidb {

struct OutColumn {
    std::string table;   // table/alias qualifier
    std::string name;
    TypeId      type;
};
using OutSchema = std::vector<OutColumn>;

// Resolve a (table, name) reference to a column index in `schema`; -1 if none,
// -2 if ambiguous (matches multiple unqualified columns).
int resolve_column(const OutSchema& schema, const std::string& table,
                   const std::string& name);

Value eval_expr(const Expr* e, const Tuple& row, const OutSchema& schema);
bool  eval_pred(const Expr* e, const Tuple& row, const OutSchema& schema);

// Physical table handle the scans operate over.
struct TableAccess {
    TableInfo* info;
    HeapFile*  heap;
};

class Operator {
public:
    virtual ~Operator() = default;
    virtual void open() = 0;
    virtual std::optional<Tuple> next() = 0;
    virtual void close() = 0;
    const OutSchema& schema() const { return out_schema_; }

protected:
    OutSchema out_schema_;
};
using OpPtr = std::unique_ptr<Operator>;

// ---- scans ---------------------------------------------------------------
class SeqScan : public Operator {
public:
    SeqScan(TableAccess ta, std::string alias);
    void open() override;
    std::optional<Tuple> next() override;
    void close() override {}
private:
    TableAccess ta_;
    std::vector<Tuple> rows_;
    size_t pos_ = 0;
};

// Primary-key index scan over an inclusive [low, high] range (either may be null).
class IndexScan : public Operator {
public:
    IndexScan(TableAccess ta, std::string alias,
              std::optional<Value> low, std::optional<Value> high);
    void open() override;
    std::optional<Tuple> next() override;
    void close() override {}
private:
    TableAccess ta_;
    std::optional<Value> low_, high_;
    std::vector<Tuple> rows_;
    size_t pos_ = 0;
};

// ---- relational operators -------------------------------------------------
class Filter : public Operator {
public:
    Filter(OpPtr child, ExprPtr pred);
    void open() override { child_->open(); }
    std::optional<Tuple> next() override;
    void close() override { child_->close(); }
private:
    OpPtr child_;
    ExprPtr pred_;
};

class NestedLoopJoin : public Operator {
public:
    NestedLoopJoin(OpPtr left, OpPtr right, ExprPtr pred);
    void open() override;
    std::optional<Tuple> next() override;
    void close() override { left_->close(); right_->close(); }
private:
    OpPtr left_, right_;
    ExprPtr pred_;
    std::optional<Tuple> cur_left_;
    std::vector<Tuple> right_rows_;   // right side materialized once
    size_t right_pos_ = 0;
};

class Projection : public Operator {
public:
    Projection(OpPtr child, const std::vector<SelectItem>& items);
    void open() override { child_->open(); }
    std::optional<Tuple> next() override;
    void close() override { child_->close(); }
private:
    OpPtr child_;
    std::vector<int> col_indexes_;   // index into child row per output column
};

// One output column of an Aggregate: either a grouping key column passed
// through, or an aggregate function over a child column.
struct AggOutput {
    bool is_group = false;
    int  group_index = 0;     // index into group_cols_ when is_group
    AggFunc func = AggFunc::NONE;
    int  col_index = -1;      // child column for the aggregate arg (-1 for *)
    bool star = false;
    std::string name;         // output column name
    TypeId type = TypeId::INTEGER;
};

class Aggregate : public Operator {
public:
    Aggregate(OpPtr child, std::vector<int> group_cols, std::vector<AggOutput> outputs);
    void open() override;
    std::optional<Tuple> next() override;
    void close() override { child_->close(); }
private:
    OpPtr child_;
    std::vector<int> group_cols_;
    std::vector<AggOutput> outputs_;
    std::vector<Tuple> output_;
    size_t pos_ = 0;
};

class Sort : public Operator {
public:
    Sort(OpPtr child, int col, bool desc);
    void open() override;
    std::optional<Tuple> next() override;
    void close() override { child_->close(); }
private:
    OpPtr child_;
    int col_;
    bool desc_;
    std::vector<Tuple> rows_;
    size_t pos_ = 0;
};

}  // namespace minidb
