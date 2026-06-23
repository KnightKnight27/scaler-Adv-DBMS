// ---------------------------------------------------------------------------
// executor.h - the query execution engine.
//
// This is where every other subsystem meets. The executor:
//   * asks the optimizer how to read each table (seq scan vs index scan)
//   * pulls tuples from the heap through the buffer pool
//   * applies MVCC visibility (or 2PL shared locks) so a transaction sees a
//     consistent view
//   * evaluates WHERE / JOIN predicates (the Lab 5 expression evaluator)
//   * runs a nested-loop join, then filters and projects
//   * routes every write through the lock manager and the WAL
//
// It deliberately materialises intermediate results into vectors: the datasets
// in this project are small enough that clarity beats a streaming iterator model.
// ---------------------------------------------------------------------------
#pragma once
#include "../catalog/catalog.h"
#include "../optimizer/optimizer.h"
#include "../txn/transaction.h"
#include "../txn/lock_manager.h"
#include "../recovery/wal.h"
#include "../sql/ast.h"
#include <unordered_map>
#include <sstream>

namespace minidb {

struct ResultSet {
    std::vector<std::string> columns;
    std::vector<Row>         rows;
    std::string              plan; // filled when EXPLAIN is used
};

using NamedRow = std::unordered_map<std::string, Value>;

class Executor {
public:
    Executor(Catalog* cat, TransactionManager* tm, LockManager* lm, WAL* wal,
             Optimizer* opt, bool mvcc_mode)
        : cat_(cat), tm_(tm), lm_(lm), wal_(wal), opt_(opt), mvcc_mode_(mvcc_mode) {}

    void set_mvcc(bool on) { mvcc_mode_ = on; }

    // ---- DDL ----
    void create_table(CreateStmt& c) {
        cat_->create_table(c.table, c.schema);
    }

    // ---- INSERT ----
    void insert(InsertStmt& ins, TxId txid) {
        TableInfo* t = require_table(ins.table);
        if (ins.values.size() != t->schema.size())
            throw std::runtime_error("column count mismatch for INSERT into " + t->name);
        Row row = ins.values;

        int64_t pk = 0;
        bool has_pk = t->pk_index >= 0;
        if (has_pk) {
            pk = as_int(row[t->pk_index]);
            lm_->acquire(lock_key(t->name, pk), txid, LockMode::EXCLUSIVE);
        }
        RID rid = t->heap->insert(row, txid);
        if (t->index) t->index->insert(pk, rid);
        wal_->log_insert(txid, t->name, pk, row);
        if (auto* tx = tm_->get(txid)) tx->inserted.push_back(rid);
    }

    // Direct row insert used by recovery redo (bypasses WAL, uses a known xmin).
    void redo_insert(const std::string& table, const Row& row, TxId xmin) {
        TableInfo* t = require_table(table);
        RID rid = t->heap->insert(row, xmin);
        if (t->index && t->pk_index >= 0) t->index->insert(as_int(row[t->pk_index]), rid);
    }

    // ---- DELETE ----
    int remove(DeleteStmt& d, TxId txid) {
        TableInfo* t = require_table(d.table);
        auto tuples = read_visible(t, txid, d.where);
        int count = 0;
        for (auto& st : tuples) {
            NamedRow nr = make_named(t, st.row, t->name, /*qualify=*/false);
            if (d.where && !eval_pred(d.where.get(), nr)) continue;
            int64_t pk = (t->pk_index >= 0) ? as_int(st.row[t->pk_index]) : 0;
            if (t->pk_index >= 0)
                lm_->acquire(lock_key(t->name, pk), txid, LockMode::EXCLUSIVE);
            t->heap->set_xmax(st.rid, txid);
            wal_->log_delete(txid, t->name, pk);
            if (auto* tx = tm_->get(txid)) tx->deleted.push_back(st.rid);
            count++;
        }
        return count;
    }

    // ---- SELECT ----
    ResultSet select(SelectStmt& s, TxId txid) {
        ResultSet rs;
        TableInfo* left = require_table(s.table);
        std::ostringstream plan;

        // Access path for the (left) table.
        AccessPath ap = opt_->choose_access(left, s.where);
        auto left_tuples = read_visible(left, txid, s.where);

        std::vector<NamedRow> rows;
        std::vector<std::string> out_cols;

        if (!s.has_join) {
            for (auto& st : left_tuples)
                rows.push_back(make_named(left, st.row, left->name, /*qualify=*/false));
            if (s.explain) {
                plan << "QUERY PLAN\n";
                plan << "  " << (ap.method == AccessPath::INDEX_SCAN ? "Index Scan" : "Seq Scan")
                     << " on " << left->name << "\n";
                plan << "    -> " << ap.reason << "\n";
                plan << "    -> est_rows=" << (long)ap.est_rows << " est_cost=" << ap.est_cost << "\n";
            }
        } else {
            TableInfo* right = require_table(s.join_table);
            auto right_tuples = read_visible(right, txid, nullptr);

            // Optimizer picks the smaller relation as the outer loop.
            bool swap = opt_->should_swap_join(left, right);
            TableInfo* outerT = swap ? right : left;
            TableInfo* innerT = swap ? left : right;
            auto& outer = swap ? right_tuples : left_tuples;
            auto& inner = swap ? left_tuples : right_tuples;

            for (auto& o : outer) {
                NamedRow onr = make_named(outerT, o.row, outerT->name, /*qualify=*/true);
                for (auto& in : inner) {
                    NamedRow merged = onr;
                    NamedRow inr = make_named(innerT, in.row, innerT->name, /*qualify=*/true);
                    for (auto& [k, v] : inr) merged[k] = v;
                    if (s.join_cond && !eval_pred(s.join_cond.get(), merged)) continue;
                    rows.push_back(std::move(merged));
                }
            }
            if (s.explain) {
                plan << "QUERY PLAN\n";
                plan << "  Nested Loop Join\n";
                plan << "    outer: Seq Scan on " << outerT->name
                     << " (rows=" << (long)opt_->cardinality(outerT) << ")\n";
                plan << "    inner: Seq Scan on " << innerT->name
                     << " (rows=" << (long)opt_->cardinality(innerT) << ")\n";
                plan << "    -> smaller relation chosen as outer to minimise inner rescans\n";
            }
        }

        // WHERE filter (applies after join so it can reference either table).
        std::vector<NamedRow> filtered;
        for (auto& nr : rows) {
            if (s.where && !eval_pred(s.where.get(), nr)) continue;
            filtered.push_back(nr);
        }

        // Projection.
        out_cols = projection_columns(s, left);
        rs.columns = out_cols;
        for (auto& nr : filtered) {
            Row out;
            for (auto& c : out_cols) {
                auto it = nr.find(c);
                out.push_back(it == nr.end() ? Value(std::string("")) : it->second);
            }
            rs.rows.push_back(std::move(out));
        }
        rs.plan = plan.str();
        return rs;
    }

private:
    // Read the tuples of `table` that this transaction may see.
    std::vector<StoredTuple> read_visible(TableInfo* t, TxId txid, const ExprPtr& where) {
        std::vector<StoredTuple> out;
        AccessPath ap = opt_->choose_access(t, where);

        auto consider = [&](StoredTuple& st) {
            if (!readable(st, txid)) return;
            if (!mvcc_mode_ && t->pk_index >= 0) {
                int64_t pk = as_int(st.row[t->pk_index]);
                lm_->acquire(lock_key(t->name, pk), txid, LockMode::SHARED);
            }
            out.push_back(st);
        };

        if (ap.method == AccessPath::INDEX_SCAN && t->index) {
            RID rid = t->index->search(ap.key);
            if (rid.valid()) {
                StoredTuple st;
                if (t->heap->get(rid, st)) consider(st);
            }
        } else {
            for (auto& st : t->heap->scan()) consider(st);
        }
        return out;
    }

    bool readable(const StoredTuple& st, TxId txid) {
        if (mvcc_mode_) return tm_->visible(st.xmin, st.xmax, txid);
        // 2PL mode: a version is the current committed row if its creator
        // committed (or is us) and it has NOT been deleted by a *committed*
        // transaction (an uncommitted delete does not yet remove the row - the
        // reader will instead block on the shared lock for that row).
        bool xmin_ok   = st.xmin == txid || tm_->is_committed(st.xmin);
        bool xmax_dead = st.xmax != INVALID_TX &&
                         (st.xmax == txid || tm_->is_committed(st.xmax));
        return xmin_ok && !xmax_dead;
    }

    NamedRow make_named(TableInfo* t, const Row& row, const std::string& tname, bool qualify) {
        NamedRow nr;
        for (size_t i = 0; i < t->schema.size(); ++i) {
            nr[t->schema[i].name] = row[i];                       // unqualified
            if (qualify) nr[tname + "." + t->schema[i].name] = row[i]; // qualified
        }
        return nr;
    }

    std::vector<std::string> projection_columns(SelectStmt& s, TableInfo* left) {
        if (!s.columns.empty()) return s.columns;
        std::vector<std::string> cols;
        if (!s.has_join) {
            for (auto& c : left->schema) cols.push_back(c.name);
        } else {
            TableInfo* right = require_table(s.join_table);
            for (auto& c : left->schema)  cols.push_back(left->name  + "." + c.name);
            for (auto& c : right->schema) cols.push_back(right->name + "." + c.name);
        }
        return cols;
    }

    // ---- expression evaluation (Lab 5 evaluator, on typed values) ----
    Value eval_value(Expr* e, NamedRow& row) {
        switch (e->kind) {
            case Expr::Kind::IntLit: return (int64_t)e->int_val;
            case Expr::Kind::StrLit: return e->str_val;
            case Expr::Kind::Column: {
                std::string key = e->table.empty() ? e->column : (e->table + "." + e->column);
                auto it = row.find(key);
                if (it == row.end() && !e->table.empty()) it = row.find(e->column);
                if (it == row.end()) throw std::runtime_error("unknown column: " + key);
                return it->second;
            }
            default: throw std::runtime_error("expected a value expression");
        }
    }

    bool eval_pred(Expr* e, NamedRow& row) {
        if (e->kind != Expr::Kind::Binary) throw std::runtime_error("invalid predicate");
        if (e->op == "AND") return eval_pred(e->left.get(), row) && eval_pred(e->right.get(), row);
        if (e->op == "OR")  return eval_pred(e->left.get(), row) || eval_pred(e->right.get(), row);
        Value a = eval_value(e->left.get(), row);
        Value b = eval_value(e->right.get(), row);
        return compare(a, b, e->op);
    }

    bool compare(const Value& a, const Value& b, const std::string& op) {
        bool both_int = std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b);
        if (both_int) {
            int64_t x = std::get<int64_t>(a), y = std::get<int64_t>(b);
            if (op == "=")  return x == y;
            if (op == "!=") return x != y;
            if (op == "<")  return x <  y;
            if (op == ">")  return x >  y;
            if (op == "<=") return x <= y;
            if (op == ">=") return x >= y;
        }
        std::string x = value_to_string(a), y = value_to_string(b);
        if (op == "=")  return x == y;
        if (op == "!=") return x != y;
        if (op == "<")  return x <  y;
        if (op == ">")  return x >  y;
        if (op == "<=") return x <= y;
        if (op == ">=") return x >= y;
        throw std::runtime_error("unknown operator: " + op);
    }

    TableInfo* require_table(const std::string& name) {
        TableInfo* t = cat_->get(name);
        if (!t) throw std::runtime_error("no such table: " + name);
        return t;
    }

    static std::string lock_key(const std::string& table, int64_t pk) {
        return table + "#" + std::to_string(pk);
    }

    Catalog*            cat_;
    TransactionManager* tm_;
    LockManager*        lm_;
    WAL*                wal_;
    Optimizer*          opt_;
    bool                mvcc_mode_;
};

} // namespace minidb
