#pragma once

#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

#include "execution/abstract_executor.h"
#include "execution/executor_context.h"
#include "parser/ast.h"
#include "parser/expression.h"

namespace minidb {

// Thrown on a semantic planning error (unknown table/column, arity mismatch,
// malformed JOIN, ...). The REPL/tests catch it and surface `what()`.
class PlanError : public std::runtime_error {
public:
    explicit PlanError(const std::string& msg) : std::runtime_error(msg) {}
};

// One output column of a scan/join, used to bind column references to physical
// positions. `table` is the source table name (for qualified resolution);
// `name` is the column name.
struct BoundColumn {
    std::string table;
    std::string name;
};

// The Planner turns a parsed Statement AST into a physical executor pipeline
// (the Volcano operator tree). It is the bridge between the parser front-end
// and the execution engine, and it is where index-aware decisions are made: a
// single-column equality predicate on an indexed column is routed through the
// cost-based Optimizer (which may pick an IndexScan), while everything else
// falls back to a SeqScan plus a rich expression FilterExecutor.
class Planner {
public:
    explicit Planner(ExecutorContext* context) : context_(context) {}

    // Build a pipeline for a SELECT (scan/join -> filter -> projection).
    std::unique_ptr<AbstractExecutor> PlanSelect(const SelectStatement& stmt);

    // Build a pipeline for a DELETE (scan -> filter -> delete).
    std::unique_ptr<AbstractExecutor> PlanDelete(const DeleteStatement& stmt);

    // Build a pipeline for an INSERT (values -> insert).
    std::unique_ptr<AbstractExecutor> PlanInsert(const InsertStatement& stmt);

private:
    ExecutorContext* context_;

    // Builds the row source for a single table, applying an optional predicate.
    // Uses the Optimizer/IndexScan when `where` is a single equality on an
    // indexed column; otherwise SeqScan (+ expression filter). `where` is
    // cloned and bound internally, so the caller's AST is left untouched.
    std::unique_ptr<AbstractExecutor> BuildSingleTableSource(
        const std::string& table_name, const Expression* where);

    // Builds a SeqScan x SeqScan nested-loop join for the SELECT's JOIN clause,
    // and reports the combined output-column layout via `out_columns`.
    std::unique_ptr<AbstractExecutor> BuildJoin(
        const SelectStatement& stmt, std::vector<BoundColumn>* out_columns);

    // Rough row-count estimate for the cost model (memtable + sstable heuristic).
    size_t EstimateTableSize(const struct TableMetadata* table) const;
};

} // namespace minidb
