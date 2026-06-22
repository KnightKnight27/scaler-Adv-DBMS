#include "executor.hpp"
#include "optimizer.hpp"
#include <memory>
#include <stdexcept>

namespace minidb {

// ---------------------------------------------------------------------------
// Column resolution: operators label each output column with its qualified
// name ("table.col"). A reference resolves either exactly (if qualified) or by
// unique unqualified suffix.
// ---------------------------------------------------------------------------
static std::string tail(const std::string& c) {
    auto d = c.find('.');
    return d == std::string::npos ? c : c.substr(d + 1);
}

static int resolveCol(const std::vector<std::string>& cols, const std::string& name) {
    bool qualified = name.find('.') != std::string::npos;
    int found = -1;
    for (size_t i = 0; i < cols.size(); i++) {
        bool match = qualified ? (cols[i] == name) : (tail(cols[i]) == name);
        if (match) {
            if (found != -1) throw std::runtime_error("ambiguous column: " + name);
            found = static_cast<int>(i);
        }
    }
    if (found == -1) throw std::runtime_error("unknown column: " + name);
    return found;
}

// Evaluate a WHERE expression against one row given the operator's columns.
static bool evalExpr(const Expr* e, const std::vector<std::string>& cols,
                     const std::vector<Value>& row) {
    if (!e) return true;
    if (e->kind == Expr::Kind::AND) return evalExpr(e->left.get(), cols, row) &&
                                            evalExpr(e->right.get(), cols, row);
    if (e->kind == Expr::Kind::OR) return evalExpr(e->left.get(), cols, row) ||
                                           evalExpr(e->right.get(), cols, row);
    int idx = resolveCol(cols, e->col);
    int c = row[idx].compare(e->val);
    switch (e->op) {
        case Expr::Op::EQ: return c == 0;
        case Expr::Op::NE: return c != 0;
        case Expr::Op::LT: return c < 0;
        case Expr::Op::LE: return c <= 0;
        case Expr::Op::GT: return c > 0;
        case Expr::Op::GE: return c >= 0;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Volcano pull operators: open() then repeated next(out).
// ---------------------------------------------------------------------------
namespace {

struct Operator {
    std::vector<std::string> columns;
    virtual ~Operator() = default;
    virtual void open() = 0;
    virtual bool next(std::vector<Value>& out) = 0;
};

// Helper: qualified column names for a table.
static std::vector<std::string> qualifiedCols(const Table* t) {
    std::vector<std::string> c;
    for (const Column& col : t->schema.columns()) c.push_back(t->name + "." + col.name);
    return c;
}

struct SeqScan : Operator {
    const Table* t_;
    std::vector<std::vector<Value>> rows_;
    size_t i_ = 0;
    explicit SeqScan(const Table* t) : t_(t) { columns = qualifiedCols(t); }
    void open() override {
        rows_.clear();
        i_ = 0;
        t_->heap->scan([&](RowId, const char* b, uint16_t l) {
            rows_.push_back(t_->schema.deserialize(std::string(b, l)));
        });
    }
    bool next(std::vector<Value>& out) override {
        if (i_ >= rows_.size()) return false;
        out = rows_[i_++];
        return true;
    }
};

struct IndexScan : Operator {
    const Table* t_;
    int64_t key_;
    std::vector<std::vector<Value>> rows_;
    size_t i_ = 0;
    IndexScan(const Table* t, int64_t key) : t_(t), key_(key) { columns = qualifiedCols(t); }
    void open() override {
        rows_.clear();
        i_ = 0;
        t_->index->rangeScan(key_, key_, [&](int64_t, RowId rid) {
            std::string bytes;
            if (t_->heap->get(rid, bytes))
                rows_.push_back(t_->schema.deserialize(bytes));
        });
    }
    bool next(std::vector<Value>& out) override {
        if (i_ >= rows_.size()) return false;
        out = rows_[i_++];
        return true;
    }
};

struct Filter : Operator {
    std::unique_ptr<Operator> child_;
    const Expr* pred_;
    Filter(std::unique_ptr<Operator> c, const Expr* p) : child_(std::move(c)), pred_(p) {
        columns = child_->columns;
    }
    void open() override { child_->open(); }
    bool next(std::vector<Value>& out) override {
        std::vector<Value> row;
        while (child_->next(row))
            if (evalExpr(pred_, columns, row)) { out = std::move(row); return true; }
        return false;
    }
};

struct Project : Operator {
    std::unique_ptr<Operator> child_;
    std::vector<int> idx_;
    Project(std::unique_ptr<Operator> c, const std::vector<std::string>& names)
        : child_(std::move(c)) {
        for (const std::string& n : names) {
            int i = resolveCol(child_->columns, n);
            idx_.push_back(i);
            columns.push_back(child_->columns[i]);
        }
    }
    void open() override { child_->open(); }
    bool next(std::vector<Value>& out) override {
        std::vector<Value> row;
        if (!child_->next(row)) return false;
        out.clear();
        for (int i : idx_) out.push_back(row[i]);
        return true;
    }
};

struct NestedLoopJoin : Operator {
    std::unique_ptr<Operator> outer_, inner_;
    int p1_ = -1, p2_ = -1;
    std::vector<std::vector<Value>> inner_rows_;
    std::vector<Value> cur_outer_;
    bool have_outer_ = false;
    size_t inner_i_ = 0;
    NestedLoopJoin(std::unique_ptr<Operator> outer, std::unique_ptr<Operator> inner,
                   const std::string& c1, const std::string& c2)
        : outer_(std::move(outer)), inner_(std::move(inner)) {
        columns = outer_->columns;
        for (const std::string& c : inner_->columns) columns.push_back(c);
        p1_ = resolveCol(columns, c1);
        p2_ = resolveCol(columns, c2);
    }
    void open() override {
        outer_->open();
        inner_->open();
        inner_rows_.clear();
        std::vector<Value> r;
        while (inner_->next(r)) inner_rows_.push_back(r);
        have_outer_ = false;
        inner_i_ = 0;
    }
    bool next(std::vector<Value>& out) override {
        while (true) {
            if (!have_outer_) {
                if (!outer_->next(cur_outer_)) return false;
                have_outer_ = true;
                inner_i_ = 0;
            }
            while (inner_i_ < inner_rows_.size()) {
                std::vector<Value> combined = cur_outer_;
                const std::vector<Value>& in = inner_rows_[inner_i_++];
                combined.insert(combined.end(), in.begin(), in.end());
                if (combined[p1_].compare(combined[p2_]) == 0) { out = std::move(combined); return true; }
            }
            have_outer_ = false; // exhausted inner; pull the next outer row
        }
    }
};

struct CountStar : Operator {
    std::unique_ptr<Operator> child_;
    bool emitted_ = false;
    explicit CountStar(std::unique_ptr<Operator> c) : child_(std::move(c)) {
        columns = {"count"};
    }
    void open() override { child_->open(); emitted_ = false; }
    bool next(std::vector<Value>& out) override {
        if (emitted_) return false;
        int64_t n = 0;
        std::vector<Value> row;
        while (child_->next(row)) n++;
        out = {Value::makeInt(n)};
        emitted_ = true;
        return true;
    }
};

// Choose SeqScan vs IndexScan for a single table using the optimizer.
static std::unique_ptr<Operator> buildScan(const Table* t, const Expr* where, bool verbose) {
    Optimizer::ScanChoice ch = Optimizer::chooseScan(t, where, verbose);
    if (ch.use_index) return std::make_unique<IndexScan>(t, ch.key);
    return std::make_unique<SeqScan>(t);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Statement execution.
// ---------------------------------------------------------------------------
ExecResult Executor::execute(const Statement& st) {
    switch (st.kind) {
        case Statement::Kind::CREATE: return runCreate(st.create);
        case Statement::Kind::INSERT: return runInsert(st.insert);
        case Statement::Kind::SELECT: return runSelect(st.select);
        case Statement::Kind::DELETE: return runDelete(st.del);
        case Statement::Kind::BEGIN:    db_.begin();    return {false, {}, {}, "BEGIN"};
        case Statement::Kind::COMMIT:   db_.commit();   return {false, {}, {}, "COMMIT"};
        case Statement::Kind::ROLLBACK: db_.rollback(); return {false, {}, {}, "ROLLBACK"};
    }
    throw std::runtime_error("unknown statement");
}

ExecResult Executor::runCreate(const CreateStmt& s) {
    if (s.cols.empty() || s.cols[0].type != Type::INT)
        throw std::runtime_error("column 0 must be an INT primary key");
    db_.createTable(s.table, Schema(s.cols));
    return {false, {}, {}, "CREATE TABLE " + s.table};
}

ExecResult Executor::runInsert(const InsertStmt& s) {
    Table* t = db_.getTable(s.table);
    if (!t) throw std::runtime_error("no such table: " + s.table);
    const auto& cols = t->schema.columns();
    if (s.values.size() != cols.size())
        throw std::runtime_error("INSERT column count mismatch");
    for (size_t i = 0; i < cols.size(); i++)
        if (s.values[i].type != cols[i].type)
            throw std::runtime_error("type mismatch for column " + cols[i].name);
    db_.insertRow(t, s.values);
    return {false, {}, {}, "INSERT 1"};
}

ExecResult Executor::runSelect(const SelectStmt& s) {
    Table* t = db_.getTable(s.table);
    if (!t) throw std::runtime_error("no such table: " + s.table);

    std::unique_ptr<Operator> op;
    if (s.join.present) {
        Table* r = db_.getTable(s.join.table);
        if (!r) throw std::runtime_error("no such table: " + s.join.table);
        bool right_inner = Optimizer::rightIsInner(t, r, true);
        // Inner relation is materialised, so the smaller table is the inner.
        std::unique_ptr<Operator> outer = std::make_unique<SeqScan>(right_inner ? t : r);
        std::unique_ptr<Operator> inner = std::make_unique<SeqScan>(right_inner ? r : t);
        op = std::make_unique<NestedLoopJoin>(std::move(outer), std::move(inner),
                                              s.join.left_col, s.join.right_col);
        if (s.where) op = std::make_unique<Filter>(std::move(op), s.where.get());
    } else {
        op = buildScan(t, s.where.get(), true);
        if (s.where) op = std::make_unique<Filter>(std::move(op), s.where.get());
    }

    if (s.count_star) op = std::make_unique<CountStar>(std::move(op));
    else if (!s.star) op = std::make_unique<Project>(std::move(op), s.cols);

    ExecResult res;
    res.is_query = true;
    res.columns = op->columns;
    op->open();
    std::vector<Value> row;
    while (op->next(row)) res.rows.push_back(row);
    return res;
}

ExecResult Executor::runDelete(const DeleteStmt& s) {
    Table* t = db_.getTable(s.table);
    if (!t) throw std::runtime_error("no such table: " + s.table);

    // Find the primary keys of all matching rows, then delete them by key.
    std::unique_ptr<Operator> scan = buildScan(t, s.where.get(), true);
    std::unique_ptr<Operator> op =
        s.where ? std::make_unique<Filter>(std::move(scan), s.where.get()) : std::move(scan);

    std::vector<int64_t> keys;
    op->open();
    std::vector<Value> row;
    while (op->next(row)) keys.push_back(row[0].i); // column 0 is the pk
    op.reset(); // release the scan before mutating the table

    int deleted = 0;
    for (int64_t k : keys)
        if (db_.deleteByKey(t, k)) deleted++;
    return {false, {}, {}, "DELETE " + std::to_string(deleted)};
}

} // namespace minidb
