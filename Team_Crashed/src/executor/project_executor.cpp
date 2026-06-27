// =============================================================================
// src/executor/project_executor.cpp
// -----------------------------------------------------------------------------
// ProjectExecutor: emit a Tuple containing only the requested columns /
// expressions from the child.
//
// Two modes:
//   1. projectionExprs is non-empty -> evaluate each expression in turn
//      against the child's row. Column refs resolve by name; function
//      calls and literals evaluate to their scalar Value. The aggregate
//      executor has already run by the time we get here, so any
//      aggregate functions arrive as already-evaluated Values embedded
//      in the child's tuple (the aggregate executor emits them as the
//      group's tuple values).
//   2. projectionExprs is empty, outputColumns is non-empty -> pick
//      each named column by index from the child's tuple.
//   3. both empty -> pass through unchanged.
// =============================================================================
#include "executor/project_executor.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "catalog/schema.h"
#include "catalog/table_info.h"
#include "executor/executor.h"
#include "executor/predicate_eval.h"
#include "parser/ast.h"

namespace minidb::executor {

ProjectExecutor::ProjectExecutor(ExecutorContext* ctx,
                                 std::unique_ptr<Executor> child,
                                 std::vector<std::string> outputColumns,
                                 std::vector<std::unique_ptr<parser::Expr>> projectionExprs,
                                 catalog::Schema childSchema)
    : Executor(ctx), child_(std::move(child)),
      outputColumns_(std::move(outputColumns)),
      projectionExprs_(std::move(projectionExprs)),
      childSchema_(std::move(childSchema)) {}

ProjectExecutor::~ProjectExecutor() = default;

Status ProjectExecutor::init() {
    if (!child_) return Status::INVALID_ARGUMENT;
    return child_->init();
}

namespace {

// Build a synthetic Schema from the child's tuple (column index -> name
// "col_i"). We use this when the project expressions are bare column
// refs that come from a join and the catalog has no single schema that
// covers them. The QueryEngine assembles the right schema before
// constructing us when it knows better.
catalog::Schema syntheticSchemaFromTuple(const Tuple& t) {
    catalog::Schema s;
    for (std::size_t i = 0; i < t.values.size(); ++i) {
        catalog::Column c;
        c.name = "col_" + std::to_string(i);
        c.type = catalog::Type::INT;
        s.addColumn(c);
    }
    return s;
}

} // namespace

Status ProjectExecutor::next(Tuple& out) {
    if (!child_) return Status::DONE;
    Tuple t;
    Status s = child_->next(t);
    if (s != Status::OK) return s;

    if (!projectionExprs_.empty()) {
        // Evaluate each expression. For COLUMN refs we look the value
        // up by name in the child's row. We synthesise a "col_N" name
        // for each position in the tuple so that bare column names in
        // the projection that come from a join can still resolve.
        // The query engine should pass a more precise schema here in
        // future revisions.
        catalog::Schema synth = childSchema_.numColumns() == 0
            ? syntheticSchemaFromTuple(t)
            : childSchema_;
        out.values.clear();
        for (const auto& e : projectionExprs_) {
            if (!e) {
                out.values.push_back(Value::makeNull());
                continue;
            }
            if (e->kind == parser::ExprKind::COLUMN) {
                // First try by name; fall back to positional "col_i".
                Value v = resolveColumn(t, synth, e->text);
                if (v.tag == Value::Tag::NULL_) {
                    // Try by simple column name from the catalog
                    // (single-table case).
                    for (std::size_t i = 0; i < t.values.size(); ++i) {
                        if (("col_" + std::to_string(i)) == e->text) {
                            v = t.values[i];
                            break;
                        }
                    }
                }
                out.values.push_back(v);
            } else {
                // Function calls / literals — by the time we get here
                // the aggregate executor has already evaluated them, so
                // what the child emitted is the final value. For pure
                // literals, evaluate now.
                out.values.push_back(evalLiteral(*e));
            }
        }
        return Status::OK;
    }

    if (!outputColumns_.empty()) {
        out.values.clear();
        for (const auto& name : outputColumns_) {
            if (childSchema_.numColumns() != 0) out.values.push_back(resolveColumn(t, childSchema_, name));
            else if (!t.values.empty()) out.values.push_back(t.values.front());
            else out.values.push_back(Value::makeNull());
        }
        return Status::OK;
    }

    out = std::move(t);
    return Status::OK;
}

Status ProjectExecutor::close() {
    if (child_) return child_->close();
    return Status::OK;
}

} // namespace minidb::executor
