// =============================================================================
// src/executor/join.cpp
// -----------------------------------------------------------------------------
// NestedLoopJoinExecutor and HashJoinExecutor.
//
// NLJ: for every left tuple, scan the entire right child and emit the
// concatenation whenever the ON predicate is satisfied.
//
// HJ : build a multimap keyed by the qualified value of the build side's
// join column, then probe with each tuple from the probe side.
//
// Both executors now resolve column references in the ON predicate by
// NAME through the supplied schemas. The previous version only looked at
// `t.values.front()`, which failed as soon as a row had more than one
// column (the bug fix).
//
// The pending-match buffer for HashJoinExecutor is kept in a file-local
// thread_local map so we can stay within the header's storage without
// modifying the public API.
// =============================================================================
#include "executor/join.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "catalog/schema.h"
#include "catalog/table_info.h"
#include "common/status.h"
#include "executor/executor.h"
#include "executor/predicate_eval.h"
#include "parser/ast.h"

namespace minidb::executor {

namespace {

// File-local per-instance pending buffer for HashJoinExecutor.
std::unordered_map<const HashJoinExecutor*, std::vector<Tuple>>&
pendingStore() {
    thread_local std::unordered_map<const HashJoinExecutor*,
                                    std::vector<Tuple>> store;
    return store;
}

// Concatenate two tuples, left first then right.
Tuple concat(const Tuple& l, const Tuple& r) {
    Tuple out;
    out.values.reserve(l.values.size() + r.values.size());
    for (const auto& v : l.values) out.values.push_back(v);
    for (const auto& v : r.values) out.values.push_back(v);
    return out;
}

// Three-way compare for join keys. NULL sorts as "less than everything".
int compareJoinValues(const Value& a, const Value& b) {
    if (a.tag == Value::Tag::NULL_ || b.tag == Value::Tag::NULL_) return 0;
    if (a.tag == Value::Tag::INT && b.tag == Value::Tag::INT) {
        return (a.i < b.i) ? -1 : (a.i > b.i ? 1 : 0);
    }
    if (a.tag == Value::Tag::FLOAT && b.tag == Value::Tag::FLOAT) {
        return (a.f < b.f) ? -1 : (a.f > b.f ? 1 : 0);
    }
    if (a.tag == Value::Tag::INT && b.tag == Value::Tag::FLOAT) {
        float av = static_cast<float>(a.i);
        return (av < b.f) ? -1 : (av > b.f ? 1 : 0);
    }
    if (a.tag == Value::Tag::FLOAT && b.tag == Value::Tag::INT) {
        float bv = static_cast<float>(b.i);
        return (a.f < bv) ? -1 : (a.f > bv ? 1 : 0);
    }
    if (a.tag == Value::Tag::STRING && b.tag == Value::Tag::STRING) {
        if (a.s == b.s) return 0;
        return a.s < b.s ? -1 : 1;
    }
    if (a.tag == Value::Tag::BOOL && b.tag == Value::Tag::BOOL) {
        return (a.b == b.b) ? 0 : (a.b ? 1 : -1);
    }
    return 0;
}

// Resolve a column reference to a Value using the supplied schema. The
// schema's column order defines tuple positions. We also strip an
// optional table-name prefix so "a.id" resolves the same way as "id"
// when there is only one such column.
Value resolveOnColumn(const parser::Expr* a, const Tuple& t,
                      const catalog::Schema& schema) {
    if (!a) return Value::makeNull();
    if (a->kind != parser::ExprKind::COLUMN) return Value::makeNull();
    return resolveColumn(t, schema, a->text);
}

// Evaluate the ON predicate between a left/right tuple pair. The
// predicate must be a binary comparison whose operands are column refs
// on each side. We resolve each operand using the supplied schema.
bool matchesOn(const parser::Expr& on,
               const Tuple& l, const Tuple& r,
               const catalog::Schema& lSchema,
               const catalog::Schema& rSchema) {
    if (on.kind != parser::ExprKind::BINARY_OP) return true;
    if (on.args.size() < 2) return true;
    Value lv = resolveOnColumn(on.args[0].get(), l, lSchema);
    if (lv.tag == Value::Tag::NULL_) lv = resolveOnColumn(on.args[0].get(), r, rSchema);
    Value rv = resolveOnColumn(on.args[1].get(), r, rSchema);
    if (rv.tag == Value::Tag::NULL_) rv = resolveOnColumn(on.args[1].get(), l, lSchema);
    int cmp = compareJoinValues(lv, rv);
    if (on.op == "=")  return cmp == 0;
    if (on.op == "!=") return cmp != 0;
    if (on.op == "<")  return cmp <  0;
    if (on.op == "<=") return cmp <= 0;
    if (on.op == ">")  return cmp >  0;
    if (on.op == ">=") return cmp >= 0;
    return true;
}

// Pick the value used as the hash key for one side of a join. We
// extract the column name from the predicate (always a column ref on
// each side) and look it up in the supplied schema.
Value joinKey(const parser::Expr& on, const Tuple& t,
              const catalog::Schema& schema, bool leftSide) {
    if (on.kind != parser::ExprKind::BINARY_OP) {
        return t.values.empty() ? Value::makeNull() : t.values.front();
    }
    if (on.args.size() < 2) {
        return t.values.empty() ? Value::makeNull() : t.values.front();
    }
    // On a binary "lhs.col = rhs.col" predicate, the LEFT side is
    // args[0] and the RIGHT side is args[1]. For the build/probe
    // orientation we don't actually care which is which — we just need
    // the *operand for this side*.
    const parser::Expr* me = leftSide ? on.args[0].get() : on.args[1].get();
    Value v = resolveOnColumn(me, t, schema);
    if (v.tag != Value::Tag::NULL_) return v;
    const parser::Expr* other = leftSide ? on.args[1].get() : on.args[0].get();
    return resolveOnColumn(other, t, schema);
}

} // namespace

// ----- NestedLoopJoinExecutor -----

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext* ctx,
                                               std::unique_ptr<Executor> left,
                                               std::unique_ptr<Executor> right,
                                               std::unique_ptr<parser::Expr> on,
                                               catalog::Schema leftSchema,
                                               catalog::Schema rightSchema)
    : Executor(ctx), left_(std::move(left)), right_(std::move(right)),
      on_(std::move(on)),
      leftSchema_(std::move(leftSchema)),
      rightSchema_(std::move(rightSchema)) {}

NestedLoopJoinExecutor::~NestedLoopJoinExecutor() = default;

// Init both children eagerly.
Status NestedLoopJoinExecutor::init() {
    Status s = left_->init();
    if (s != Status::OK) return s;
    s = right_->init();
    if (s != Status::OK) return s;
    leftReady_ = false;
    return Status::OK;
}

// Classic NLJ: for each left tuple, scan every right tuple; emit on match.
Status NestedLoopJoinExecutor::next(Tuple& out) {
    while (true) {
        if (!leftReady_) {
            Status s = left_->next(curLeft_);
            if (s != Status::OK) return Status::DONE;
            // Reset right for the new left tuple.
            (void)right_->close();
            Status rs = right_->init();
            if (rs != Status::OK) return rs;
            leftReady_ = true;
        }
        Tuple r;
        Status rs = right_->next(r);
        if (rs == Status::OK) {
            if (!on_) {
                out = concat(curLeft_, r);
                return Status::OK;
            }
            if (matchesOn(*on_, curLeft_, r, leftSchema_, rightSchema_)) {
                out = concat(curLeft_, r);
                return Status::OK;
            }
            continue;
        }
        // Right side exhausted; advance left.
        leftReady_ = false;
    }
}

Status NestedLoopJoinExecutor::close() {
    if (left_)  (void)left_->close();
    if (right_) (void)right_->close();
    return Status::OK;
}

// ----- HashJoinExecutor -----

HashJoinExecutor::HashJoinExecutor(ExecutorContext* ctx,
                                   std::unique_ptr<Executor> build,
                                   std::unique_ptr<Executor> probe,
                                   std::unique_ptr<parser::Expr> on,
                                   catalog::Schema buildSchema,
                                   catalog::Schema probeSchema)
    : Executor(ctx), build_(std::move(build)), probe_(std::move(probe)),
      on_(std::move(on)),
      buildSchema_(std::move(buildSchema)),
      probeSchema_(std::move(probeSchema)) {}

// Out-of-line destructor: also clear the file-local pending buffer.
HashJoinExecutor::~HashJoinExecutor() {
    pendingStore().erase(this);
}

// Build phase: drain the build side into the multimap, keyed by the
// join-key column value resolved through buildSchema_.
Status HashJoinExecutor::init() {
    Status s = build_->init();
    if (s != Status::OK) return s;
    hash_.clear();
    Tuple t;
    while (build_->next(t) == Status::OK) {
        Value k = on_ ? joinKey(*on_, t, buildSchema_, /*leftSide=*/true)
                      : (t.values.empty() ? Value::makeNull() : t.values.front());
        hash_.emplace(k.toString(), std::move(t));
    }
    (void)build_->close();
    pendingStore()[this].clear();
    return probe_->init();
}

// Probe phase: look up each probe tuple's join-key value in the
// multimap and emit concatenated matches.
Status HashJoinExecutor::next(Tuple& out) {
    auto& pending = pendingStore()[this];
    while (true) {
        if (!probeReady_) {
            pending.clear();
            Status s = probe_->next(curProbe_);
            if (s != Status::OK) return Status::DONE;
            Value k = on_ ? joinKey(*on_, curProbe_, probeSchema_, /*leftSide=*/false)
                          : (curProbe_.values.empty() ? Value::makeNull() : curProbe_.values.front());
            auto range = hash_.equal_range(k.toString());
            for (auto it = range.first; it != range.second; ++it) {
                pending.push_back(it->second);
            }
            if (pending.empty()) continue;     // no match: try next probe
            probeReady_ = true;
        }
        if (!pending.empty()) {
            Tuple left = pending.front();
            pending.erase(pending.begin());
            out = concat(left, curProbe_);
            if (pending.empty()) probeReady_ = false;
            return Status::OK;
        }
        probeReady_ = false;
    }
}

Status HashJoinExecutor::close() {
    if (probe_) (void)probe_->close();
    hash_.clear();
    pendingStore().erase(this);
    return Status::OK;
}

} // namespace minidb::executor
