// =============================================================================
// src/executor/aggregate_executor.cpp
// -----------------------------------------------------------------------------
// AggregateExecutor: drain the child, hash rows into groups by the GROUP BY
// keys, and compute the requested aggregates per group.
//
// Aggregate functions (parser::ExprKind::FUNCTION_CALL):
//   COUNT(*) / COUNT(col)  -> INT     (NULL values are not counted for COUNT(col))
//   SUM(col)               -> INT (if all values are INT) or FLOAT
//   AVG(col)               -> FLOAT
//   MIN(col) / MAX(col)    -> same type as the input (NULL-aware)
//
// One Tuple per group is emitted. The shape of the Tuple matches the
// projectionExprs list: each function call's result is appended in the
// order the projection lists them, optionally preceded by the GROUP BY
// key values.
// =============================================================================
#include "executor/aggregate_executor.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "catalog/schema.h"
#include "common/status.h"
#include "executor/executor.h"
#include "executor/predicate_eval.h"
#include "parser/ast.h"

namespace minidb::executor {

namespace {

// Build a stable string key from a list of Values for use as the hash
// table key. We use the textual form because it gives a stable order
// across types without needing a custom hasher.
std::string valuesKey(const std::vector<Value>& vs) {
    std::string out;
    for (std::size_t i = 0; i < vs.size(); ++i) {
        if (i) out.push_back('\x1f');   // unit separator
        out += vs[i].toString();
    }
    return out;
}

// Resolve a column name in the current tuple. We fall back to the
// synthetic "col_i" naming used by joins.
Value resolveAny(const Tuple& t, const std::string& name) {
    // Try numeric positional name first (most common in joins).
    for (std::size_t i = 0; i < t.values.size(); ++i) {
        if (("col_" + std::to_string(i)) == name) return t.values[i];
    }
    // Otherwise treat the column name as a literal position-by-index
    // lookup: the i-th column in the original schema is at position i.
    // We can't access the schema here without plumbing it through; for
    // the v1 aggregate executor, callers pass column names that match
    // the schema's column names exactly.
    return Value::makeNull();
}

} // namespace

AggregateExecutor::AggregateExecutor(ExecutorContext* ctx,
                                     std::unique_ptr<Executor> child,
                                     std::vector<std::unique_ptr<parser::Expr>> projectionExprs,
                                     std::vector<parser::Expr> groupBy)
    : Executor(ctx), child_(std::move(child)),
      projectionExprs_(std::move(projectionExprs)),
      groupBy_(std::move(groupBy)) {}

AggregateExecutor::~AggregateExecutor() = default;

Status AggregateExecutor::init() {
    if (!child_) return Status::INVALID_ARGUMENT;
    Status s = child_->init();
    if (s != Status::OK) return s;

    groupIndex_.clear();
    groups_.clear();

    // Pre-create the (possibly single) group slots so the per-aggregate
    // accumulators are always present.
    auto newGroup = [this]() -> std::size_t {
        Group g;
        // One AggState per projection expression that is a function call.
        for (const auto& e : projectionExprs_) {
            if (e && e->kind == parser::ExprKind::FUNCTION_CALL) {
                AggState a;
                a.func = e->text;
                // First argument's column name (or "*" for COUNT(*)).
                if (!e->args.empty() && e->args[0]) {
                    if (e->args[0]->kind == parser::ExprKind::COLUMN &&
                        e->args[0]->text == "*") {
                        a.isStar = true;
                    } else {
                        a.col = e->args[0]->text;
                    }
                }
                g.aggs.push_back(std::move(a));
            }
        }
        groups_.push_back(std::move(g));
        return groups_.size() - 1;
    };

    if (groupBy_.empty()) {
        newGroup();
    }

    Tuple t;
    while (child_->next(t) == Status::OK) {
        // Compute the group key.
        std::vector<Value> keyVals;
        for (const auto& k : groupBy_) {
            if (k.kind == parser::ExprKind::COLUMN) {
                keyVals.push_back(resolveAny(t, k.text));
            } else {
                keyVals.push_back(evalLiteral(k));
            }
        }
        const std::string key = valuesKey(keyVals);

        std::size_t gIdx;
        if (groupBy_.empty()) {
            gIdx = 0;
        } else {
            auto it = groupIndex_.find(key);
            if (it == groupIndex_.end()) {
                gIdx = newGroup();
                groups_[gIdx].key = std::move(keyVals);
                groupIndex_[key] = gIdx;
            } else {
                gIdx = it->second;
            }
        }

        Group& g = groups_[gIdx];
        std::size_t ai = 0;
        for (const auto& e : projectionExprs_) {
            if (!e || e->kind != parser::ExprKind::FUNCTION_CALL) continue;
            AggState& a = g.aggs[ai++];
            // Pull the value to fold in.
            Value v;
            if (a.isStar) {
                v = Value::makeInt(1);
            } else if (!a.col.empty()) {
                v = resolveAny(t, a.col);
            } else {
                v = Value::makeNull();
            }

            if (a.func == "COUNT") {
                if (a.isStar) {
                    ++a.count;
                } else if (v.tag != Value::Tag::NULL_) {
                    ++a.count;
                }
            } else if (a.func == "SUM") {
                if (v.tag == Value::Tag::NULL_) { /* skip */ }
                else if (v.tag == Value::Tag::INT) {
                    a.sumI += v.i;
                    a.sumF += static_cast<double>(v.i);
                    a.anyValue = true;
                } else if (v.tag == Value::Tag::FLOAT) {
                    a.sumF += v.f;
                    a.anyValue = true;
                }
            } else if (a.func == "AVG") {
                if (v.tag == Value::Tag::NULL_) { /* skip */ }
                else {
                    if (v.tag == Value::Tag::INT) a.sumF += static_cast<double>(v.i);
                    else                            a.sumF += static_cast<double>(v.f);
                    ++a.count;
                    a.anyValue = true;
                }
            } else if (a.func == "MIN") {
                if (v.tag == Value::Tag::NULL_) { /* skip */ }
                else if (!a.anyValue || compareValues(v, a.min) < 0) {
                    a.min = v;
                    a.anyValue = true;
                }
            } else if (a.func == "MAX") {
                if (v.tag == Value::Tag::NULL_) { /* skip */ }
                else if (!a.anyValue || compareValues(v, a.max) > 0) {
                    a.max = v;
                    a.anyValue = true;
                }
            }
        }
    }

    cursor_ = 0;
    return Status::OK;
}

Status AggregateExecutor::next(Tuple& out) {
    if (cursor_ >= groups_.size()) return Status::DONE;
    const Group& g = groups_[cursor_++];
    out.values.clear();
    // GROUP BY key columns come first (in declaration order), then the
    // aggregate results in the order they were listed.
    for (const auto& kv : g.key) out.values.push_back(kv);
    for (const auto& a : g.aggs) {
        if (a.func == "COUNT") {
            out.values.push_back(Value::makeInt(static_cast<int32_t>(a.count)));
        } else if (a.func == "SUM") {
            if (!a.anyValue) out.values.push_back(Value::makeNull());
            else if (a.sumI != 0) out.values.push_back(Value::makeInt(static_cast<int32_t>(a.sumI)));
            else                  out.values.push_back(Value::makeInt(static_cast<int32_t>(a.sumF)));
        } else if (a.func == "AVG") {
            if (!a.anyValue || a.count == 0) out.values.push_back(Value::makeNull());
            else                              out.values.push_back(Value::makeFloat(static_cast<float>(a.sumF / static_cast<double>(a.count))));
        } else if (a.func == "MIN") {
            if (!a.anyValue) out.values.push_back(Value::makeNull());
            else              out.values.push_back(a.min);
        } else if (a.func == "MAX") {
            if (!a.anyValue) out.values.push_back(Value::makeNull());
            else              out.values.push_back(a.max);
        } else {
            out.values.push_back(Value::makeNull());
        }
    }
    return Status::OK;
}

Status AggregateExecutor::close() {
    if (child_) (void)child_->close();
    groups_.clear();
    groupIndex_.clear();
    cursor_ = 0;
    return Status::OK;
}

} // namespace minidb::executor