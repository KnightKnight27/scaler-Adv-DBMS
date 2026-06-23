// MiniDB - planner: turns a parsed Statement into a tree of physical operators, asking the
// Optimizer for the access path and join order. Produces an EXPLAIN string alongside the plan.
#pragma once

#include <memory>
#include <string>

#include "../catalog/catalog.h"
#include "../execution/executors.h"
#include "../parser/ast.h"

namespace minidb {

struct PhysicalPlan {
    std::unique_ptr<Executor> exec;
    std::string explain;
};

class Planner {
public:
    // ctx is threaded into leaf/DML operators so they take 2PL locks (may be null = no locking).
    Planner(Catalog* cat, ExecutionContext* ctx = nullptr) : cat_(cat), ctx_(ctx) {}

    // Only SELECT/INSERT/DELETE produce a plan; DDL and txn control are handled by Database.
    PhysicalPlan Plan(Statement* stmt);

private:
    PhysicalPlan PlanSelect(SelectStmt* s);
    PhysicalPlan PlanInsert(InsertStmt* s);
    PhysicalPlan PlanDelete(DeleteStmt* s);

    Catalog* cat_;
    ExecutionContext* ctx_;
};

}  // namespace minidb
