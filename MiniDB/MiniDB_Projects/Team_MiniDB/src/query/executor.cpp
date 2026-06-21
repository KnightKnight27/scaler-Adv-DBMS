#include "executor.hpp"

#include <climits>
#include <cmath>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "../catalog/catalog.hpp"
#include "optimizer.hpp"

namespace {

// ── Runtime row + schema ────────────────────────────────────────────────────
// A Row is just the column values. An ExecSchema labels each value with its
// (table, name, type) so expressions can resolve column references by name —
// including across a join, where two tables' columns sit side by side.
using Row = std::vector<Value>;

struct ColInfo { std::string table; std::string name; ColumnType type; };
using ExecSchema = std::vector<ColInfo>;

ExecSchema schema_of(const Table& t) {
    ExecSchema s;
    for (const Column& c : t.schema.columns) s.push_back({t.name, c.name, c.type});
    return s;
}

// Resolve a (maybe-qualified) column reference to its index in `s`.
int resolve(const ExecSchema& s, const std::string& table, const std::string& name) {
    int found = -1;
    for (int i = 0; i < static_cast<int>(s.size()); ++i) {
        if (s[i].name == name && (table.empty() || s[i].table == table)) {
            if (found != -1) throw std::runtime_error("ambiguous column: " + name);
            found = i;
        }
    }
    if (found == -1) throw std::runtime_error("unknown column: " + name);
    return found;
}

// ── Expression evaluation ───────────────────────────────────────────────────
Value eval_operand(const Expr* e, const Row& row, const ExecSchema& s) {
    if (auto* c = dynamic_cast<const ColumnRef*>(e)) return row[static_cast<std::size_t>(resolve(s, c->table, c->name))];
    if (auto* l = dynamic_cast<const Literal*>(e)) return l->val;
    throw std::runtime_error("expected a column or literal operand");
}

bool eval_pred(const Expr* e, const Row& row, const ExecSchema& s) {
    const auto* b = dynamic_cast<const BinaryExpr*>(e);
    if (!b) throw std::runtime_error("predicate must be a comparison");
    if (b->op == "AND") return eval_pred(b->left.get(), row, s) && eval_pred(b->right.get(), row, s);
    if (b->op == "OR")  return eval_pred(b->left.get(), row, s) || eval_pred(b->right.get(), row, s);
    int c = value_compare(eval_operand(b->left.get(), row, s), eval_operand(b->right.get(), row, s));
    if (b->op == "=")  return c == 0;
    if (b->op == "!=") return c != 0;
    if (b->op == "<")  return c < 0;
    if (b->op == ">")  return c > 0;
    if (b->op == "<=") return c <= 0;
    if (b->op == ">=") return c >= 0;
    throw std::runtime_error("unknown operator: " + b->op);
}

// ── Operators (Volcano pull model: open(), then next() until nullopt) ────────
struct Operator {
    virtual ~Operator() = default;
    virtual const ExecSchema& schema() const = 0;
    virtual void open() = 0;
    virtual std::optional<Row> next() = 0;
};

// Full table scan over the heap.
class SeqScan : public Operator {
public:
    explicit SeqScan(Table* t) : t_(t), schema_(schema_of(*t)) {}
    const ExecSchema& schema() const override { return schema_; }
    void open() override { rows_ = t_->heap->scan(); pos_ = 0; }
    std::optional<Row> next() override {
        if (pos_ >= rows_.size()) return std::nullopt;
        return t_->schema.deserialize(rows_[pos_++].second);
    }
private:
    Table*      t_;
    ExecSchema  schema_;
    std::vector<std::pair<RowID, std::string>> rows_;
    std::size_t pos_ = 0;
};

// Scan only the keys in [low, high] via the B+ tree, then fetch by RowID.
class IndexScan : public Operator {
public:
    IndexScan(Table* t, int low, int high) : t_(t), schema_(schema_of(*t)), low_(low), high_(high) {}
    const ExecSchema& schema() const override { return schema_; }
    void open() override { rids_ = t_->index->range(low_, high_); pos_ = 0; }
    std::optional<Row> next() override {
        while (pos_ < rids_.size()) {
            std::optional<std::string> bytes = t_->heap->get(rids_[pos_++]);
            if (bytes) return t_->schema.deserialize(*bytes);
        }
        return std::nullopt;
    }
private:
    Table*              t_;
    ExecSchema          schema_;
    int                 low_, high_;
    std::vector<RowID>  rids_;
    std::size_t         pos_ = 0;
};

// Pass through only rows satisfying the predicate.
class Filter : public Operator {
public:
    Filter(std::unique_ptr<Operator> child, const Expr* pred)
        : child_(std::move(child)), pred_(pred) {}
    const ExecSchema& schema() const override { return child_->schema(); }
    void open() override { child_->open(); }
    std::optional<Row> next() override {
        for (std::optional<Row> r = child_->next(); r; r = child_->next())
            if (eval_pred(pred_, *r, child_->schema())) return r;
        return std::nullopt;
    }
private:
    std::unique_ptr<Operator> child_;
    const Expr*               pred_;
};

// Keep only the selected columns (SELECT col, col, ...).
class Project : public Operator {
public:
    Project(std::unique_ptr<Operator> child, const std::vector<SelectCol>& cols)
        : child_(std::move(child)) {
        for (const SelectCol& sc : cols) {
            int i = resolve(child_->schema(), sc.table, sc.name);
            idx_.push_back(i);
            schema_.push_back(child_->schema()[static_cast<std::size_t>(i)]);
        }
    }
    const ExecSchema& schema() const override { return schema_; }
    void open() override { child_->open(); }
    std::optional<Row> next() override {
        std::optional<Row> r = child_->next();
        if (!r) return std::nullopt;
        Row out;
        out.reserve(idx_.size());
        for (int i : idx_) out.push_back((*r)[static_cast<std::size_t>(i)]);
        return out;
    }
private:
    std::unique_ptr<Operator> child_;
    std::vector<int>          idx_;
    ExecSchema                schema_;
};

// Nested-loop join: for each left row, scan the (materialized) right rows and
// emit the concatenation where the ON predicate holds.
class NestedLoopJoin : public Operator {
public:
    NestedLoopJoin(std::unique_ptr<Operator> l, std::unique_ptr<Operator> r, std::unique_ptr<Expr> on)
        : left_(std::move(l)), right_(std::move(r)), on_(std::move(on)) {
        schema_ = left_->schema();
        for (const ColInfo& c : right_->schema()) schema_.push_back(c);
    }
    const ExecSchema& schema() const override { return schema_; }
    void open() override {
        left_->open();
        right_->open();
        right_rows_.clear();
        for (std::optional<Row> r = right_->next(); r; r = right_->next()) right_rows_.push_back(*r);
        left_cur_ = left_->next();
        right_idx_ = 0;
    }
    std::optional<Row> next() override {
        while (left_cur_) {
            while (right_idx_ < right_rows_.size()) {
                const Row& rr = right_rows_[right_idx_++];
                Row combined = *left_cur_;
                combined.insert(combined.end(), rr.begin(), rr.end());
                if (eval_pred(on_.get(), combined, schema_)) return combined;
            }
            left_cur_ = left_->next();
            right_idx_ = 0;
        }
        return std::nullopt;
    }
private:
    std::unique_ptr<Operator> left_, right_;
    std::unique_ptr<Expr>     on_;
    ExecSchema                schema_;
    std::vector<Row>          right_rows_;
    std::optional<Row>        left_cur_;
    std::size_t               right_idx_ = 0;
};

// ── Planning ────────────────────────────────────────────────────────────────
// Pre-optimizer rule (Phase 2): if the WHERE is a single top-level comparison
// of the primary-key column against an int literal, scan via the index over the
// implied key range. A residual Filter(WHERE) always sits on top, so the index
// range only has to be a SUPERSET — correctness never depends on its precision.
// The Phase 3 cost optimizer generalizes this choice.
std::optional<std::pair<int, int>> detect_pk_range(const Expr* where, const Table& t) {
    const auto* b = dynamic_cast<const BinaryExpr*>(where);
    if (!b || b->op == "AND" || b->op == "OR") return std::nullopt;
    const auto* col = dynamic_cast<const ColumnRef*>(b->left.get());
    const auto* lit = dynamic_cast<const Literal*>(b->right.get());
    if (!col || !lit || !std::holds_alternative<int>(lit->val)) return std::nullopt;
    if (col->name != t.schema.columns[static_cast<std::size_t>(t.pk_col)].name) return std::nullopt;
    if (!col->table.empty() && col->table != t.name) return std::nullopt;
    int v = std::get<int>(lit->val);
    if (b->op == "=")  return std::make_pair(v, v);
    if (b->op == ">")  return std::make_pair(v == INT_MAX ? INT_MAX : v + 1, INT_MAX);
    if (b->op == ">=") return std::make_pair(v, INT_MAX);
    if (b->op == "<")  return std::make_pair(INT_MIN, v == INT_MIN ? INT_MIN : v - 1);
    if (b->op == "<=") return std::make_pair(INT_MIN, v);
    return std::nullopt;
}

// Cost-based scan choice. A SeqScan reads every row (cost = N). An IndexScan is
// possible only when WHERE has a sargable predicate on the PK; its cost is the
// estimated matching rows plus the B+ descent (~log N). We pick the cheaper —
// so a selective `pk = k` uses the index, while `pk != k` (matches almost all)
// or a non-PK predicate falls back to a sequential scan.
std::unique_ptr<Operator> scan_for(Table& t, const Expr* where, std::ostream& out) {
    double n = std::max(1.0, static_cast<double>(t.row_count));
    std::optional<std::pair<int, int>> rng = where ? detect_pk_range(where, t) : std::nullopt;

    if (rng) {
        double matched = estimate_cardinality(t, where);
        double cost_index = matched + std::log2(n + 1.0);
        double cost_seq = n;
        out << "  [opt] " << t.name << ": seqscan=" << static_cast<long>(cost_seq)
            << " vs indexscan=" << static_cast<long>(cost_index);
        if (cost_index < cost_seq) {
            out << " -> INDEX SCAN over [" << rng->first << ", " << rng->second << "]\n";
            return std::make_unique<IndexScan>(&t, rng->first, rng->second);
        }
        out << " -> SEQ SCAN\n";
        return std::make_unique<SeqScan>(&t);
    }
    out << "  [opt] " << t.name << ": no PK predicate -> SEQ SCAN (cost=" << static_cast<long>(n) << ")\n";
    return std::make_unique<SeqScan>(&t);
}

std::unique_ptr<Operator> plan_select(const SelectStmt& s, Catalog& cat, std::ostream& out) {
    Table* t = cat.get_table(s.from);
    if (!t) throw std::runtime_error("no such table: " + s.from);

    std::unique_ptr<Operator> root;
    if (s.has_join) {
        Table* t2 = cat.get_table(s.join_table);
        if (!t2) throw std::runtime_error("no such table: " + s.join_table);

        // Cost-based join order. We materialize the inner side, so the nA*nB
        // comparison work is order-independent; the tie-break is materialization
        // cost, so we make the SMALLER relation the inner. (Pushing the WHERE
        // into the join inputs is future work — inputs are full scans for now.)
        double nA = std::max(1.0, static_cast<double>(t->row_count));
        double nB = std::max(1.0, static_cast<double>(t2->row_count));
        Table* outer = t; Table* inner = t2;       // default keeps B as inner
        if (nA < nB) { outer = t2; inner = t; }     // smaller relation becomes inner
        out << "  [opt] join " << t->name << "(" << static_cast<long>(nA) << ") x "
            << t2->name << "(" << static_cast<long>(nB) << "): outer=" << outer->name
            << ", inner=" << inner->name << " (smaller side materialized)\n";

        auto on = std::make_unique<BinaryExpr>();
        on->op = "=";
        auto l = std::make_unique<ColumnRef>(); l->table = s.jl_table; l->name = s.jl_col;
        auto r = std::make_unique<ColumnRef>(); r->table = s.jr_table; r->name = s.jr_col;
        on->left = std::move(l);
        on->right = std::move(r);
        root = std::make_unique<NestedLoopJoin>(std::make_unique<SeqScan>(outer),
                                                std::make_unique<SeqScan>(inner),
                                                std::move(on));
        if (s.where) root = std::make_unique<Filter>(std::move(root), s.where.get());
    } else {
        root = scan_for(*t, s.where.get(), out);
        if (s.where) root = std::make_unique<Filter>(std::move(root), s.where.get());
    }
    if (!s.star) root = std::make_unique<Project>(std::move(root), s.columns);
    return root;
}

// ── Output formatting ───────────────────────────────────────────────────────
// Header names are qualified ("table.col") only when a bare name is ambiguous.
std::vector<std::string> header_names(const ExecSchema& s) {
    std::vector<std::string> names;
    for (const ColInfo& c : s) {
        int count = 0;
        for (const ColInfo& o : s) if (o.name == c.name) ++count;
        names.push_back(count > 1 && !c.table.empty() ? c.table + "." + c.name : c.name);
    }
    return names;
}

void print_row(std::ostream& out, const Row& row) {
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (i) out << " | ";
        out << value_to_string(row[i]);
    }
    out << "\n";
}

// ── Statement executors ─────────────────────────────────────────────────────
void exec_create(const CreateStmt& s, Catalog& cat, std::ostream& out) {
    cat.create_table(s.table, s.schema, s.pk_col);
    out << "table '" << s.table << "' created\n";
}

void exec_insert(const InsertStmt& s, Catalog& cat, std::ostream& out) {
    Table* t = cat.get_table(s.table);
    if (!t) throw std::runtime_error("no such table: " + s.table);
    if (s.values.size() != t->schema.columns.size())
        throw std::runtime_error("column count mismatch");
    for (std::size_t k = 0; k < s.values.size(); ++k) {
        bool is_int = std::holds_alternative<int>(s.values[k]);
        bool want_int = t->schema.columns[k].type == ColumnType::INT;
        if (is_int != want_int)
            throw std::runtime_error("type mismatch for column '" + t->schema.columns[k].name + "'");
    }
    int key = std::get<int>(s.values[static_cast<std::size_t>(t->pk_col)]);
    if (t->index->search(key)) throw std::runtime_error("duplicate primary key");
    RowID rid = t->heap->insert(t->schema.serialize(s.values));
    t->index->insert(key, rid);
    ++t->row_count;
    out << "1 row inserted\n";
}

void exec_delete(const DeleteStmt& s, Catalog& cat, std::ostream& out) {
    Table* t = cat.get_table(s.table);
    if (!t) throw std::runtime_error("no such table: " + s.table);
    ExecSchema sch = schema_of(*t);
    int n = 0;
    for (auto& [rid, bytes] : t->heap->scan()) {  // scan() is a snapshot; safe to erase
        Row row = t->schema.deserialize(bytes);
        if (!s.where || eval_pred(s.where.get(), row, sch)) {
            t->heap->erase(rid);
            t->index->remove(std::get<int>(row[static_cast<std::size_t>(t->pk_col)]));
            ++n;
        }
    }
    t->row_count -= static_cast<std::size_t>(n);
    out << n << " row(s) deleted\n";
}

void exec_select(const SelectStmt& s, Catalog& cat, std::ostream& out) {
    std::unique_ptr<Operator> root = plan_select(s, cat, out);
    root->open();

    std::vector<std::string> names = header_names(root->schema());
    for (std::size_t i = 0; i < names.size(); ++i) { if (i) out << " | "; out << names[i]; }
    out << "\n";

    int n = 0;
    for (std::optional<Row> r = root->next(); r; r = root->next()) { print_row(out, *r); ++n; }
    out << "(" << n << " row(s))\n";
}

}  // namespace

void execute_statement(const Statement& stmt, Catalog& catalog, std::ostream& out) {
    if (auto* c = std::get_if<CreateStmt>(&stmt)) exec_create(*c, catalog, out);
    else if (auto* i = std::get_if<InsertStmt>(&stmt)) exec_insert(*i, catalog, out);
    else if (auto* s = std::get_if<SelectStmt>(&stmt)) exec_select(*s, catalog, out);
    else if (auto* d = std::get_if<DeleteStmt>(&stmt)) exec_delete(*d, catalog, out);
}
