#pragma once

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../storage/heap_file.h"
#include "../types.h"
#include "exec_context.h"
#include "parser.h"

namespace minidb {

struct Row {
    Tuple tuple;
    RID rid{-1, -1};
};

// Compare a tuple column against a predicate's literal.
inline bool eval_predicate(const Tuple& t, const Schema& sch, const Predicate& p) {
    int idx = column_index(sch, p.col);
    if (idx < 0) return false;
    const Value& v = t[idx];
    Value r = p.val;
    if (v.type == Type::Int && r.type == Type::Text) r = Value::make_int(std::strtoll(r.s.c_str(), nullptr, 10));
    int c;
    if (v.type == Type::Int)
        c = v.i < r.i ? -1 : (v.i > r.i ? 1 : 0);
    else
        c = v.s < r.s ? -1 : (v.s > r.s ? 1 : 0);
    if (p.op == "=") return c == 0;
    if (p.op == "!=") return c != 0;
    if (p.op == "<") return c < 0;
    if (p.op == ">") return c > 0;
    if (p.op == "<=") return c <= 0;
    if (p.op == ">=") return c >= 0;
    return false;
}

// Volcano-style iterator. Operators pull rows one at a time via next().
class Operator {
public:
    Schema out_schema;
    virtual ~Operator() = default;
    virtual void open() = 0;
    virtual bool next(Row& out) = 0;
    virtual void close() {}
};

// Reads every live tuple of a table, taking a shared lock on each.
class SeqScan : public Operator {
public:
    SeqScan(ExecContext* ctx, const std::string& table) : ctx_(ctx), table_(table) {
        TableInfo& ti = ctx_->cat->get(table_);
        for (const Column& c : ti.schema) out_schema.push_back({table_ + "." + c.name, c.type});
    }
    void open() override {
        TableInfo& ti = ctx_->cat->get(table_);
        HeapFile heap(*ctx_->bp, ti.page_ids);
        heap.scan([&](RID rid, const std::vector<uint8_t>& b) {
            ctx_->lock_or_abort(table_, rid, LockMode::Shared);
            rows_.push_back({deserialize_tuple(ti.schema, b.data(), static_cast<int>(b.size())), rid});
        });
    }
    bool next(Row& out) override {
        if (cur_ >= rows_.size()) return false;
        out = rows_[cur_++];
        return true;
    }

private:
    ExecContext* ctx_;
    std::string table_;
    std::vector<Row> rows_;
    size_t cur_ = 0;
};

// Looks up a single key through the table's primary B+ Tree index.
class IndexScan : public Operator {
public:
    IndexScan(ExecContext* ctx, const std::string& table, int64_t key)
        : ctx_(ctx), table_(table), key_(key) {
        TableInfo& ti = ctx_->cat->get(table_);
        for (const Column& c : ti.schema) out_schema.push_back({table_ + "." + c.name, c.type});
    }
    void open() override {
        TableInfo& ti = ctx_->cat->get(table_);
        RID rid;
        if (!ti.index || !ti.index->search(key_, rid)) return;
        ctx_->lock_or_abort(table_, rid, LockMode::Shared);
        HeapFile heap(*ctx_->bp, ti.page_ids);
        std::vector<uint8_t> b;
        if (heap.get(rid, b))
            row_ = {deserialize_tuple(ti.schema, b.data(), static_cast<int>(b.size())), rid};
    }
    bool next(Row& out) override {
        if (consumed_ || row_.rid.page_id < 0) return false;
        out = row_;
        consumed_ = true;
        return true;
    }

private:
    ExecContext* ctx_;
    std::string table_;
    int64_t key_;
    Row row_;
    bool consumed_ = false;
};

class Filter : public Operator {
public:
    Filter(std::unique_ptr<Operator> child, Predicate pred)
        : child_(std::move(child)), pred_(std::move(pred)) {
        out_schema = child_->out_schema;
    }
    void open() override { child_->open(); }
    bool next(Row& out) override {
        Row r;
        while (child_->next(r))
            if (eval_predicate(r.tuple, out_schema, pred_)) {
                out = r;
                return true;
            }
        return false;
    }
    void close() override { child_->close(); }

private:
    std::unique_ptr<Operator> child_;
    Predicate pred_;
};

class Project : public Operator {
public:
    Project(std::unique_ptr<Operator> child, const std::vector<std::string>& cols)
        : child_(std::move(child)) {
        for (const std::string& c : cols) {
            int idx = column_index(child_->out_schema, c);
            idx_.push_back(idx);
            if (idx >= 0)
                out_schema.push_back({c, child_->out_schema[idx].type});
            else
                out_schema.push_back({c, Type::Text});
        }
    }
    void open() override { child_->open(); }
    bool next(Row& out) override {
        Row r;
        if (!child_->next(r)) return false;
        out.tuple.clear();
        out.rid = r.rid;
        for (int i : idx_)
            out.tuple.push_back(i >= 0 ? r.tuple[i] : Value::make_text(""));
        return true;
    }
    void close() override { child_->close(); }

private:
    std::unique_ptr<Operator> child_;
    std::vector<int> idx_;
};

// Inner equi-join. The inner (right) side is materialised once; the outer side
// streams past it. The optimizer makes the smaller relation the outer one.
class NestedLoopJoin : public Operator {
public:
    NestedLoopJoin(std::unique_ptr<Operator> left, std::unique_ptr<Operator> right,
                   std::string left_on, std::string right_on)
        : left_(std::move(left)), right_(std::move(right)),
          left_on_(std::move(left_on)), right_on_(std::move(right_on)) {
        out_schema = left_->out_schema;
        for (const Column& c : right_->out_schema) out_schema.push_back(c);
        left_idx_ = resolve(left_->out_schema, left_on_, right_on_);
        right_idx_ = resolve(right_->out_schema, left_on_, right_on_);
    }

    // The optimizer may put either table on the outer side, so match each join
    // column against whichever side actually contains it.
    static int resolve(const Schema& s, const std::string& a, const std::string& b) {
        int i = column_index(s, a);
        return i >= 0 ? i : column_index(s, b);
    }
    void open() override {
        right_->open();
        Row r;
        while (right_->next(r)) inner_.push_back(r);
        left_->open();
        j_ = inner_.size();  // force the first next() to pull an outer row
    }
    bool next(Row& out) override {
        if (inner_.empty()) return false;
        while (true) {
            if (j_ >= inner_.size()) {
                if (!left_->next(outer_)) return false;
                j_ = 0;
            }
            const Row& in = inner_[j_++];
            if (outer_.tuple[left_idx_] == in.tuple[right_idx_]) {
                out.tuple = outer_.tuple;
                out.tuple.insert(out.tuple.end(), in.tuple.begin(), in.tuple.end());
                out.rid = {-1, -1};
                return true;
            }
        }
    }
    void close() override {
        left_->close();
        right_->close();
    }

private:
    std::unique_ptr<Operator> left_, right_;
    std::string left_on_, right_on_;
    int left_idx_ = -1, right_idx_ = -1;
    std::vector<Row> inner_;
    Row outer_;
    size_t j_ = 0;
};

}  // namespace minidb
