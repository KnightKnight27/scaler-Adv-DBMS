#include "executor.h"

#include <stdexcept>

namespace minidb {

int resolveColumn(const std::vector<std::string>& outputColumns, const ColRef& ref) {
    int found = -1;
    for (size_t i = 0; i < outputColumns.size(); ++i) {
        const std::string& q = outputColumns[i];
        auto dot = q.find('.');
        std::string table = q.substr(0, dot);
        std::string col = q.substr(dot + 1);
        if (ref.name == col && (ref.table.empty() || ref.table == table)) {
            if (found != -1) throw std::runtime_error("ambiguous column '" + ref.name + "'");
            found = static_cast<int>(i);
        }
    }
    if (found == -1) throw std::runtime_error("unknown column '" + ref.name + "'");
    return found;
}

bool valueCompare(const Value& a, const std::string& op, const Value& b) {
    if (op == "=") return a == b;
    if (op == "!=") return !(a == b);
    // ordering comparisons require matching types
    if (a.type != b.type) return false;
    int cmp;
    if (a.isInt()) cmp = (a.i < b.i) ? -1 : (a.i > b.i ? 1 : 0);
    else cmp = a.s.compare(b.s) < 0 ? -1 : (a.s.compare(b.s) > 0 ? 1 : 0);
    if (op == "<") return cmp < 0;
    if (op == ">") return cmp > 0;
    if (op == "<=") return cmp <= 0;
    if (op == ">=") return cmp >= 0;
    throw std::runtime_error("unknown operator " + op);
}

namespace {

std::vector<std::string> qualifiedColumns(const std::string& table, const Schema& schema) {
    std::vector<std::string> cols;
    for (const Column& c : schema.columns) cols.push_back(table + "." + c.name);
    return cols;
}

// Full table scan straight from the heap file.
class SeqScan : public Operator {
public:
    SeqScan(TableInfo* t) : t_(t) {
        outputColumns = qualifiedColumns(t->name, t->schema);
    }
    bool next(Row& out) override {
        int pages = t_->disk->numPages();
        while (page_ < pages) {
            Page* p = t_->pool->fetch(page_);  // re-fetch each call: safe vs eviction
            if (slot_ < p->numSlots()) {
                std::string rec;
                bool live = p->get(slot_, rec);
                ++slot_;
                if (live) { out = decodeRow(rec); return true; }
            } else {
                ++page_;
                slot_ = 0;
            }
        }
        return false;
    }
private:
    TableInfo* t_;
    int page_ = 0, slot_ = 0;
};

// Jump straight to the row whose primary key == key, via the B+ tree.
class IndexScan : public Operator {
public:
    IndexScan(TableInfo* t, Value key) : t_(t), key_(std::move(key)) {
        outputColumns = qualifiedColumns(t->name, t->schema);
    }
    bool next(Row& out) override {
        if (done_) return false;
        done_ = true;
        RID rid;
        if (key_.isInt() && t_->index && t_->index->search(key_.i, rid)) {
            std::string rec;
            if (t_->heap->get(rid, rec)) { out = decodeRow(rec); return true; }
        }
        return false;
    }
private:
    TableInfo* t_;
    Value key_;
    bool done_ = false;
};

struct Pred { int idx; std::string op; Value literal; };

// Pass through only the rows that satisfy every condition (AND).
class Filter : public Operator {
public:
    Filter(std::unique_ptr<Operator> child, const std::vector<Condition>& conds)
        : child_(std::move(child)) {
        outputColumns = child_->outputColumns;
        for (const Condition& c : conds)
            preds_.push_back(Pred{resolveColumn(outputColumns, c.left), c.op, c.literal});
    }
    bool next(Row& out) override {
        Row row;
        while (child_->next(row)) {
            bool ok = true;
            for (const Pred& p : preds_)
                if (!valueCompare(row[p.idx], p.op, p.literal)) { ok = false; break; }
            if (ok) { out = std::move(row); return true; }
        }
        return false;
    }
private:
    std::unique_ptr<Operator> child_;
    std::vector<Pred> preds_;
};

// Emit only the requested columns from each child row.
class Project : public Operator {
public:
    Project(std::unique_ptr<Operator> child, const std::vector<ColRef>& cols)
        : child_(std::move(child)) {
        for (const ColRef& c : cols) {
            int idx = resolveColumn(child_->outputColumns, c);
            indices_.push_back(idx);
            outputColumns.push_back(child_->outputColumns[idx]);
        }
    }
    bool next(Row& out) override {
        Row row;
        if (!child_->next(row)) return false;
        out.clear();
        for (int i : indices_) out.push_back(row[i]);
        return true;
    }
private:
    std::unique_ptr<Operator> child_;
    std::vector<int> indices_;
};

// Nested-loop join: buffer the inner relation once, then for each outer row
// scan it looking for matches on the join key.
class NestedLoopJoin : public Operator {
public:
    NestedLoopJoin(std::unique_ptr<Operator> outer, std::unique_ptr<Operator> inner,
                   const Condition& joinCond)
        : outer_(std::move(outer)), inner_(std::move(inner)) {
        outputColumns = outer_->outputColumns;
        for (const std::string& c : inner_->outputColumns) outputColumns.push_back(c);
        leftIdx_ = resolveColumn(outputColumns, joinCond.left);
        rightIdx_ = resolveColumn(outputColumns, joinCond.right);
    }
    bool next(Row& out) override {
        if (!buffered_) {
            Row r;
            while (inner_->next(r)) innerRows_.push_back(r);
            buffered_ = true;
            haveOuter_ = outer_->next(outerRow_);
        }
        while (haveOuter_) {
            while (innerIdx_ < innerRows_.size()) {
                Row combined = outerRow_;
                const Row& ir = innerRows_[innerIdx_++];
                combined.insert(combined.end(), ir.begin(), ir.end());
                if (combined[leftIdx_] == combined[rightIdx_]) { out = std::move(combined); return true; }
            }
            innerIdx_ = 0;
            haveOuter_ = outer_->next(outerRow_);
        }
        return false;
    }
private:
    std::unique_ptr<Operator> outer_, inner_;
    std::vector<Row> innerRows_;
    Row outerRow_;
    size_t innerIdx_ = 0;
    int leftIdx_ = 0, rightIdx_ = 0;
    bool buffered_ = false, haveOuter_ = false;
};

}  // namespace

std::unique_ptr<Operator> buildOperator(const PlanNode* node, Catalog& catalog) {
    switch (node->kind) {
        case PlanKind::SeqScan:
            return std::make_unique<SeqScan>(catalog.get(node->table));
        case PlanKind::IndexScan:
            return std::make_unique<IndexScan>(catalog.get(node->table), node->key);
        case PlanKind::Filter:
            return std::make_unique<Filter>(buildOperator(node->child.get(), catalog), node->conditions);
        case PlanKind::Project:
            return std::make_unique<Project>(buildOperator(node->child.get(), catalog), node->columns);
        case PlanKind::Join:
            return std::make_unique<NestedLoopJoin>(buildOperator(node->left.get(), catalog),
                                                    buildOperator(node->right.get(), catalog),
                                                    node->joinCond);
    }
    throw std::runtime_error("unknown plan node");
}

}  // namespace minidb
