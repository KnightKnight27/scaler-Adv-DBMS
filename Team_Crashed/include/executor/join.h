// =============================================================================
// include/executor/join.h
// -----------------------------------------------------------------------------
// NestedLoopJoin and HashJoin executors. Both consume the left child fully
// per right tuple (NLJ) or after building a hash table (HJ).
//
// Each join takes the two children's Schemas as constructor arguments.
// The schemas are used to resolve column references in the ON predicate
// by name, instead of blindly picking the leading value of each tuple.
// =============================================================================
#pragma once

#include <memory>
#include <unordered_map>

#include "catalog/schema.h"
#include "executor/executor.h"
#include "parser/ast.h"

namespace minidb::executor {

class NestedLoopJoinExecutor : public Executor {
public:
    NestedLoopJoinExecutor(ExecutorContext* ctx,
                           std::unique_ptr<Executor> left,
                           std::unique_ptr<Executor> right,
                           std::unique_ptr<parser::Expr> onPredicate,
                           catalog::Schema leftSchema,
                           catalog::Schema rightSchema);
    ~NestedLoopJoinExecutor() override;

    Status init() override;
    Status next (Tuple& out) override;
    Status close() override;

private:
    std::unique_ptr<Executor> left_, right_;
    std::unique_ptr<parser::Expr> on_;
    catalog::Schema             leftSchema_;
    catalog::Schema             rightSchema_;
    Tuple                       curLeft_;
    bool                        leftReady_ = false;
};

class HashJoinExecutor : public Executor {
public:
    HashJoinExecutor(ExecutorContext* ctx,
                     std::unique_ptr<Executor> build,    // typically the right side
                     std::unique_ptr<Executor> probe,
                     std::unique_ptr<parser::Expr> onPredicate,
                     catalog::Schema buildSchema,
                     catalog::Schema probeSchema);
    ~HashJoinExecutor() override;

    Status init() override;
    Status next (Tuple& out) override;
    Status close() override;

private:
    std::unique_ptr<Executor> build_, probe_;
    std::unique_ptr<parser::Expr> on_;
    catalog::Schema             buildSchema_;
    catalog::Schema             probeSchema_;
    // Hash table is built from `build_` in init().
    std::unordered_multimap<std::string, Tuple> hash_;
    Tuple curProbe_;
    bool  probeReady_ = false;
};

} // namespace minidb::executor