#pragma once

#include <memory>
#include <string>

#include "catalog/catalog.h"
#include "engine/storage_engine.h"
#include "execution/operators.h"
#include "parser/ast.h"

namespace minidb {

// Cost-based(ish) planner: turns a SELECT AST into a Volcano operator tree.
// Decisions it makes (and records in explanation()):
//   - table scan vs index scan, from a primary-key predicate + selectivity estimate
//   - hash join vs nested-loop join, and which side to build (smaller table)
class Optimizer {
public:
    Optimizer(Catalog* cat, StorageEngine* engine) : cat_(cat), engine_(engine) {}

    std::unique_ptr<Operator> plan(const SelectStmt& stmt);

    // Human-readable description of the chosen plan (EXPLAIN-style).
    const std::string& explanation() const { return explain_; }

private:
    std::unique_ptr<Operator> build_access(const std::string& table, const std::string& alias,
                                           const Expr* where, bool allow_index);
    std::unique_ptr<Operator> build_join(const SelectStmt& stmt);
    std::unique_ptr<Operator> build_aggregate(std::unique_ptr<Operator> child, const SelectStmt& stmt);
    std::unique_ptr<Operator> build_project(std::unique_ptr<Operator> child,
                                            const std::vector<std::string>& columns);

    Catalog*       cat_;
    StorageEngine* engine_;
    std::string    explain_;
};

} // namespace minidb
