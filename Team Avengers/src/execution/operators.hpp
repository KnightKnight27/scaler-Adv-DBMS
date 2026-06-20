// ============================================================================
//  operators.hpp — Concrete Volcano operators.
//
//    SeqScan      : read every live tuple of a table, optionally filtered.
//    IndexScan    : use the primary B+ tree to read only the key range that
//                   can satisfy the predicate — the fast path.
//    NestedLoopJoin: for each outer tuple, scan the inner; emit matches on an
//                   equi-join key. Simple, always-correct join algorithm.
//    Projection   : keep only the requested output columns (SELECT list).
//
//  SeqScan and IndexScan are the two access paths the optimizer (M4) chooses
//  between. They expose the SAME interface and output schema, so the optimizer
//  can swap one for the other without the rest of the plan noticing.
// ============================================================================
#pragma once

#include "executor.hpp"

#include <limits>
#include <memory>
#include <vector>

namespace minidb {

// ---- SeqScan ---------------------------------------------------------------
class SeqScanExecutor : public Executor {
public:
    SeqScanExecutor(TableInfo* t, const Expr* filter)
        : table_(t), filter_(filter), schema_(qualify(t->name, t->schema)) {}

    void open() override {
        rids_.clear();
        idx_ = 0;
        // Materialise the live RID list up front; we still fetch tuple bytes
        // lazily in next(). (RIDs are tiny; tuples are not.)
        table_->heap->scan([&](RID rid, const std::string&) { rids_.push_back(rid); });
    }

    bool next(Tuple* out) override {
        while (idx_ < rids_.size()) {
            RID rid = rids_[idx_++];
            std::string bytes;
            if (!table_->heap->get(rid, &bytes)) continue;     // raced delete
            Tuple t = Tuple::deserialize(bytes.data(), table_->schema);
            if (eval_predicate(filter_, t, schema_)) { *out = std::move(t); return true; }
        }
        return false;
    }

    const Schema& out_schema() const override { return schema_; }

private:
    TableInfo*  table_;
    const Expr* filter_;
    Schema      schema_;
    std::vector<RID> rids_;
    size_t      idx_ = 0;
};

// ---- IndexScan -------------------------------------------------------------
// Reads only [lo, hi] of the primary key from the B+ tree, then re-checks the
// full predicate (the index narrows the candidate set; the filter is exact).
class IndexScanExecutor : public Executor {
public:
    IndexScanExecutor(TableInfo* t, int64_t lo, int64_t hi, const Expr* filter)
        : table_(t), lo_(lo), hi_(hi), filter_(filter),
          schema_(qualify(t->name, t->schema)) {}

    void open() override {
        hits_ = table_->index->range(lo_, hi_);
        idx_ = 0;
    }

    bool next(Tuple* out) override {
        while (idx_ < hits_.size()) {
            RID rid = hits_[idx_++].second;
            std::string bytes;
            if (!table_->heap->get(rid, &bytes)) continue;
            Tuple t = Tuple::deserialize(bytes.data(), table_->schema);
            if (eval_predicate(filter_, t, schema_)) { *out = std::move(t); return true; }
        }
        return false;
    }

    const Schema& out_schema() const override { return schema_; }

private:
    TableInfo*  table_;
    int64_t     lo_, hi_;
    const Expr* filter_;
    Schema      schema_;
    std::vector<std::pair<int64_t, RID>> hits_;
    size_t      idx_ = 0;
};

// ---- NestedLoopJoin --------------------------------------------------------
class NestedLoopJoinExecutor : public Executor {
public:
    NestedLoopJoinExecutor(std::unique_ptr<Executor> left, std::unique_ptr<Executor> right,
                           std::string left_col, std::string right_col)
        : left_(std::move(left)), right_(std::move(right)),
          left_col_(std::move(left_col)), right_col_(std::move(right_col)) {
        // combined schema = all left columns followed by all right columns
        for (auto& c : left_->out_schema().columns)  schema_.columns.push_back(c);
        for (auto& c : right_->out_schema().columns) schema_.columns.push_back(c);
        lc_ = find_col(left_->out_schema(),  left_col_);
        rc_ = find_col(right_->out_schema(), right_col_);
        if (lc_ < 0 || rc_ < 0) throw std::runtime_error("join column not found");
    }

    void open() override {
        left_->open();
        have_outer_ = left_->next(&outer_);
        // Buffer the inner side once (small-table assumption) so we can rescan
        // it for every outer tuple without re-opening.
        right_->open();
        inner_.clear();
        Tuple t;
        while (right_->next(&t)) inner_.push_back(t);
        inner_pos_ = 0;
    }

    bool next(Tuple* out) override {
        while (have_outer_) {
            while (inner_pos_ < inner_.size()) {
                const Tuple& in = inner_[inner_pos_++];
                if (compare_value(outer_.values[lc_], in.values[rc_]) == 0) {
                    Tuple joined = outer_;                       // copy left cols
                    joined.values.insert(joined.values.end(),
                                         in.values.begin(), in.values.end());
                    *out = std::move(joined);
                    return true;
                }
            }
            have_outer_ = left_->next(&outer_);                 // advance outer
            inner_pos_ = 0;
        }
        return false;
    }

    const Schema& out_schema() const override { return schema_; }

private:
    std::unique_ptr<Executor> left_, right_;
    std::string left_col_, right_col_;
    int lc_ = -1, rc_ = -1;
    Schema schema_;
    Tuple  outer_;
    bool   have_outer_ = false;
    std::vector<Tuple> inner_;
    size_t inner_pos_ = 0;
};

// ---- Projection ------------------------------------------------------------
// Narrows each child tuple to the SELECT list. "*" passes everything through.
class ProjectionExecutor : public Executor {
public:
    ProjectionExecutor(std::unique_ptr<Executor> child, const std::vector<std::string>& cols)
        : child_(std::move(child)) {
        const Schema& cs = child_->out_schema();
        if (cols.size() == 1 && cols[0] == "*") {
            for (int i = 0; i < (int)cs.columns.size(); ++i) { idxs_.push_back(i); schema_.columns.push_back(cs.columns[i]); }
        } else {
            for (auto& c : cols) {
                int i = find_col(cs, c);
                if (i < 0) throw std::runtime_error("unknown column in SELECT: " + c);
                idxs_.push_back(i);
                schema_.columns.push_back(cs.columns[i]);
            }
        }
    }

    void open() override { child_->open(); }

    bool next(Tuple* out) override {
        Tuple t;
        if (!child_->next(&t)) return false;
        Tuple p;
        for (int i : idxs_) p.values.push_back(t.values[i]);
        *out = std::move(p);
        return true;
    }

    const Schema& out_schema() const override { return schema_; }

private:
    std::unique_ptr<Executor> child_;
    std::vector<int> idxs_;
    Schema schema_;
};

}  // namespace minidb
