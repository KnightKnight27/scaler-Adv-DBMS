#include "engine.h"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace minidb {

// ── Schema helpers ────────────────────────────────────────────────────────────

int Schema::col_idx(const std::string& name) const {
    for (int i = 0; i < static_cast<int>(cols.size()); i++)
        if (cols[i].name == name) return i;
    return -1;
}

// ── Database constructor / destructor ─────────────────────────────────────────

Database::Database(const std::string& path)
    : db_path_(path), disk_(path + ".db"), pool_(disk_, 64) {
    // If a WAL file exists, run crash recovery before accepting new queries.
    std::ifstream check(path + ".wal");
    if (check.is_open()) recover();
    check.close();

    // Open WAL in append mode for new log records.
    wal_out_.open(path + ".wal", std::ios::app);
    if (!wal_out_.is_open())
        throw std::runtime_error("Cannot open WAL file: " + path + ".wal");
}

Database::~Database() {
    pool_.flush_all();
    wal_out_.close();
}

// ── Row encoding / decoding ───────────────────────────────────────────────────
// Format: "v1|v2|v3\n"  Values separated by '|'.
// Note: '|' inside VARCHAR values is not escaped (documented limitation).

std::string Database::encode(const Schema& sc, const std::vector<Value>& vals) {
    std::string s;
    for (int i = 0; i < static_cast<int>(vals.size()); i++) {
        if (i > 0) s += '|';
        s += vals[i].to_string();
    }
    return s;
}

std::vector<Value> Database::decode(const Schema& sc, const std::string& row) {
    std::vector<Value> vals;
    std::istringstream ss(row);
    std::string token;
    int col = 0;
    while (std::getline(ss, token, '|')) {
        if (col >= static_cast<int>(sc.cols.size())) break;
        if (sc.cols[col].type == Type::INT)
            vals.push_back(Value::make_int(std::stoi(token)));
        else
            vals.push_back(Value::make_str(token));
        col++;
    }
    return vals;
}

// ── Predicate evaluation ──────────────────────────────────────────────────────

bool Database::row_matches(const std::vector<Value>& row, const Schema& sc,
                           const std::vector<Cond>& conds,
                           const std::string& qualifier) const {
    for (const Cond& c : conds) {
        // Strip optional "table." qualifier from condition column name.
        std::string col_name = c.col;
        auto dot = col_name.find('.');
        if (dot != std::string::npos) {
            std::string tbl_prefix = col_name.substr(0, dot);
            if (!qualifier.empty() && tbl_prefix != qualifier) continue; // condition for the other table
            col_name = col_name.substr(dot + 1);
        }

        int idx = sc.col_idx(col_name);
        if (idx < 0 || idx >= static_cast<int>(row.size())) continue;

        const Value& v = row[idx];
        bool ok;
        switch (c.op) {
            case Op::EQ: ok = (v == c.val); break;
            case Op::NE: ok = (v != c.val); break;
            case Op::LT: ok = (v <  c.val); break;
            case Op::LE: ok = (v <= c.val); break;
            case Op::GT: ok = (v >  c.val); break;
            case Op::GE: ok = (v >= c.val); break;
            default: ok = false;
        }
        if (!ok) return false;
    }
    return true;
}

// ── Optimizer ─────────────────────────────────────────────────────────────────
// Cost model (simplified, but captures the real trade-off):
//   Index point lookup  → O(log N)  — use when WHERE has pk = constant
//   Index range scan    → O(log N + K) — use when WHERE has pk comparisons
//   Sequential scan     → O(N)     — fallback
//
// For equality on PK: selectivity ≈ 1/N (almost always index wins).
// For other predicates: selectivity ≈ 0.1 (default guess).

Database::Plan Database::optimize(const Table& t, const std::vector<Cond>& conds) {
    int pk = t.schema.pk;
    const std::string& pk_name = t.schema.cols[pk].name;

    // Check if any condition is an equality predicate on the primary key.
    for (const Cond& c : conds) {
        std::string col = c.col;
        if (auto dot = col.find('.'); dot != std::string::npos)
            col = col.substr(dot + 1);
        if (col == pk_name && c.op == Op::EQ) {
            return {ScanType::INDEX_POINT,
                    "IndexScan(pk=" + c.val.to_string() + ")",
                    c.val, c.val, false};
        }
    }

    // Check for range predicates on PK (combine lower and upper bounds).
    Value lo = Value::make_int(std::numeric_limits<int>::min());
    Value hi = Value::make_int(std::numeric_limits<int>::max());
    bool  range_found = false;
    for (const Cond& c : conds) {
        std::string col = c.col;
        if (auto dot = col.find('.'); dot != std::string::npos)
            col = col.substr(dot + 1);
        if (col != pk_name) continue;
        if (t.schema.cols[pk].type == Type::VARCHAR) continue; // range only for INT PKs
        range_found = true;
        if (c.op == Op::GT || c.op == Op::GE) lo = c.val;
        if (c.op == Op::LT || c.op == Op::LE) hi = c.val;
    }

    int N = std::max(1, t.row_count);
    if (range_found) {
        // Rough cost: seqscan = N, range = log2(N) + fraction of rows.
        double seq_cost  = N;
        double idx_cost  = std::log2(N) + N * 0.3;  // assume ~30% of rows in range
        if (idx_cost < seq_cost)
            return {ScanType::INDEX_RANGE,
                    "IndexScan(range " + lo.to_string() + ".." + hi.to_string() + ")",
                    lo, hi, true};
    }

    return {ScanType::SEQ, "SeqScan(" + t.name + ", rows≈" + std::to_string(N) + ")"};
}

// ── Scan executor ─────────────────────────────────────────────────────────────

std::vector<std::pair<RID, std::vector<Value>>>
Database::exec_scan(Table& t, const Plan& plan, const std::vector<Cond>& conds) {
    std::vector<std::pair<RID, std::vector<Value>>> result;

    if (plan.scan == ScanType::INDEX_POINT) {
        RID rid;
        if (t.index->search(plan.key_lo, rid) && rid.valid()) {
            std::string raw;
            if (t.heap->fetch(rid, raw)) {
                auto row = decode(t.schema, raw);
                if (row_matches(row, t.schema, conds, t.name))
                    result.push_back({rid, row});
            }
        }
        return result;
    }

    if (plan.scan == ScanType::INDEX_RANGE) {
        for (RID rid : t.index->range(plan.key_lo, plan.key_hi)) {
            std::string raw;
            if (t.heap->fetch(rid, raw)) {
                auto row = decode(t.schema, raw);
                if (row_matches(row, t.schema, conds, t.name))
                    result.push_back({rid, row});
            }
        }
        return result;
    }

    // Sequential scan: read every live row.
    for (auto& [rid, raw] : t.heap->scan()) {
        auto row = decode(t.schema, raw);
        if (row_matches(row, t.schema, conds, t.name))
            result.push_back({rid, row});
    }
    return result;
}

// ── Projection ────────────────────────────────────────────────────────────────

std::vector<Value> Database::project(const std::vector<Value>& row, const Schema& sc,
                                     const std::vector<std::string>& cols, bool star) const {
    if (star) return row;
    std::vector<Value> out;
    for (const std::string& col : cols) {
        std::string c = col;
        if (auto dot = c.find('.'); dot != std::string::npos) c = c.substr(dot + 1);
        int idx = sc.col_idx(c);
        if (idx >= 0) out.push_back(row[idx]);
    }
    return out;
}

std::vector<std::string> Database::project_names(const Schema& sc,
                                                  const std::vector<std::string>& cols,
                                                  bool star, const std::string& qualifier) const {
    if (star) {
        std::vector<std::string> names;
        for (auto& cd : sc.cols) {
            names.push_back(qualifier.empty() ? cd.name : qualifier + "." + cd.name);
        }
        return names;
    }
    return cols;
}

// ── WAL helpers ───────────────────────────────────────────────────────────────
// WAL line format:
//   CREATE tablename col:type:pk ...
//   BEGIN txnid
//   INSERT txnid tablename pk_value encoded_row
//   DELETE txnid tablename pk_value
//   COMMIT txnid
//   ABORT  txnid

void Database::wal_write(const std::string& line) {
    wal_out_ << line << '\n';
    wal_out_.flush();  // force to disk before confirming the operation
}

// ── Recovery ──────────────────────────────────────────────────────────────────
// Redo-only recovery: we replay only committed transactions.
// Uncommitted work is simply ignored — no undo phase needed because we
// never write dirty data to disk during normal operation (WAL is written
// BEFORE the data page, so the data page changes land in the buffer pool
// and are not guaranteed on disk).  In practice, our buffer pool IS flushed
// at shutdown, but in a crash scenario the WAL is the authoritative record.

void Database::recover() {
    std::ifstream wal(db_path_ + ".wal");
    if (!wal.is_open()) return;

    // Pass 1: collect committed transaction IDs.
    std::unordered_set<long> committed;
    {
        std::string line;
        while (std::getline(wal, line)) {
            if (line.rfind("COMMIT ", 0) == 0) {
                committed.insert(std::stol(line.substr(7)));
            }
        }
    }

    // Pass 2: replay CREATE and committed DML.
    wal.clear(); wal.seekg(0);
    std::string line;
    while (std::getline(wal, line)) {
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string op; ss >> op;

        if (op == "CREATE") {
            // CREATE tablename col:type:pk ...
            std::string tname; ss >> tname;
            Stmt s; s.kind = Kind::CREATE; s.table = tname;
            std::string cdef;
            while (ss >> cdef) {
                auto p1 = cdef.find(':');
                auto p2 = cdef.rfind(':');
                ColDef cd;
                cd.name = cdef.substr(0, p1);
                cd.type = cdef.substr(p1+1, p2-p1-1) == "INT" ? Type::INT : Type::VARCHAR;
                cd.pk   = cdef.substr(p2+1) == "1";
                s.cols.push_back(cd);
            }
            do_create(s);
            rec_.tables++;

        } else if (op == "INSERT") {
            long txnid; std::string tname, pk_str, encoded;
            ss >> txnid >> tname >> pk_str >> encoded;
            if (committed.count(txnid) == 0) continue;

            auto it = tables_.find(tname);
            if (it == tables_.end()) continue;
            Table& t    = it->second;
            auto   vals = decode(t.schema, encoded);
            if (vals.empty()) continue;

            Value pk_val = vals[t.schema.pk];
            RID   rid    = t.heap->insert(encoded);
            t.index->insert(pk_val, rid);
            t.row_count++;
            rec_.rows_redone++;

        } else if (op == "DELETE") {
            long txnid; std::string tname, pk_str;
            ss >> txnid >> tname >> pk_str;
            if (committed.count(txnid) == 0) continue;

            auto it = tables_.find(tname);
            if (it == tables_.end()) continue;
            Table& t = it->second;

            // Reconstruct the PK value.
            Value pk_val = (t.schema.cols[t.schema.pk].type == Type::INT)
                         ? Value::make_int(std::stoi(pk_str))
                         : Value::make_str(pk_str);
            RID rid;
            if (t.index->search(pk_val, rid) && rid.valid()) {
                t.heap->remove(rid);
                t.index->erase(pk_val);
                t.row_count--;
            }
        }
        // BEGIN, COMMIT, ABORT lines are skipped in pass 2.
    }
}

// ── CREATE TABLE ──────────────────────────────────────────────────────────────

Result Database::do_create(const Stmt& s) {
    if (tables_.count(s.table))
        return {false, "Table '" + s.table + "' already exists."};

    // Build schema.
    Schema sc;
    sc.cols = s.cols;
    for (int i = 0; i < static_cast<int>(s.cols.size()); i++) {
        if (s.cols[i].pk) { sc.pk = i; break; }
    }

    // Build WAL record (only when not called from recover()).
    // We detect "called from recover" by checking if wal_out_ is open.
    if (wal_out_.is_open()) {
        std::string rec = "CREATE " + s.table;
        for (auto& cd : s.cols) {
            rec += ' ';
            rec += cd.name + ':';
            rec += (cd.type == Type::INT ? "INT" : "VARCHAR");
            rec += cd.pk ? ":1" : ":0";
        }
        wal_write(rec);
    }

    Table t;
    t.name   = s.table;
    t.schema = sc;
    t.heap   = std::make_unique<Heap>(pool_, disk_);
    t.index  = std::make_unique<BPlusTree>(32);

    tables_.emplace(s.table, std::move(t));
    return {true, "Table '" + s.table + "' created."};
}

// ── INSERT ────────────────────────────────────────────────────────────────────

Result Database::do_insert(const Stmt& s) {
    Table& t = get_table(s.table);
    if (s.values.size() != t.schema.cols.size())
        return {false, "Column count mismatch."};

    Value pk_val = s.values[t.schema.pk];

    // Duplicate key check via index.
    RID existing;
    if (t.index->search(pk_val, existing))
        return {false, "Duplicate primary key: " + pk_val.to_string()};

    // Acquire exclusive lock on this row (2PL).
    if (cur_txn_) {
        try { locks_.acquire(cur_txn_.get(), lock_key(s.table, pk_val), true); }
        catch (DeadlockError&) {
            rollback(cur_txn_.get());
            cur_txn_.reset();
            return {false, "Deadlock detected — transaction aborted."};
        }
    }

    std::string encoded = encode(t.schema, s.values);

    // WAL first, then modify data (Write-Ahead Logging guarantee).
    long txnid = cur_txn_ ? cur_txn_->id : next_txn_id_++;
    wal_write("INSERT " + std::to_string(txnid) + " " + s.table + " " +
              pk_val.to_string() + " " + encoded);
    if (!cur_txn_) wal_write("COMMIT " + std::to_string(txnid));

    RID rid = t.heap->insert(encoded);
    t.index->insert(pk_val, rid);
    t.row_count++;

    // Record undo information so ABORT can reverse this insert.
    if (cur_txn_) {
        cur_txn_->undo.push_back({UndoRecord::INSERT, s.table, pk_val, encoded});
    }

    return {true, "1 row inserted."};
}

// ── SELECT ────────────────────────────────────────────────────────────────────

Result Database::do_select(const Stmt& s) {
    Table& t = get_table(s.table);
    Plan plan = optimize(t, s.where);

    Result res; res.ok = true; res.is_query = true;
    res.plan_desc = plan.description;
    if (s.explain) {
        res.msg = "Plan: " + plan.description
                + "\n  B+Tree height: " + std::to_string(t.index->height())
                + "\n  Estimated rows: " + std::to_string(t.row_count);
        return res;
    }

    if (!s.has_join) {
        // Simple single-table scan.
        auto rows = exec_scan(t, plan, s.where);
        res.col_names = project_names(t.schema, s.sel_cols, s.star, "");
        for (auto& [rid, row] : rows)
            res.rows.push_back(project(row, t.schema, s.sel_cols, s.star));
        return res;
    }

    // ── Nested-loop join ──────────────────────────────────────────────────────
    // Outer: t (left table), inner: join_table.
    // Optimizer choice: if the join key on the inner table is its primary key,
    // use an index probe; otherwise fall back to a full inner scan.
    Table& inner = get_table(s.join_table);

    // Determine which column of each table is the join key.
    auto split_qual = [](const std::string& s) -> std::pair<std::string,std::string> {
        auto dot = s.find('.');
        if (dot == std::string::npos) return {"", s};
        return {s.substr(0, dot), s.substr(dot+1)};
    };
    auto [ltbl, lcol] = split_qual(s.join_left);
    auto [rtbl, rcol] = split_qual(s.join_right);

    // If the join cols are for the wrong tables, swap.
    if (!ltbl.empty() && ltbl == s.join_table) {
        std::swap(lcol, rcol);
    }

    int outer_join_idx = t.schema.col_idx(lcol);
    int inner_join_idx = inner.schema.col_idx(rcol);
    bool inner_pk_join = (inner_join_idx == inner.schema.pk);

    // Build column header = outer cols + inner cols.
    std::vector<std::string> outer_names = project_names(t.schema,     s.sel_cols, false, t.name);
    std::vector<std::string> inner_names = project_names(inner.schema, s.sel_cols, false, inner.name);
    // For SELECT *, show all columns of both tables.
    if (s.star) {
        outer_names = project_names(t.schema,     {}, true, t.name);
        inner_names = project_names(inner.schema, {}, true, inner.name);
    }
    res.col_names = outer_names;
    for (auto& n : inner_names) res.col_names.push_back(n);

    std::string join_plan = inner_pk_join ? "IndexNL-Join" : "NestedLoop-Join";
    res.plan_desc = join_plan + "(" + t.name + ", " + inner.name + ")";

    // Outer loop over left table.
    Plan outer_plan = optimize(t, s.where);
    auto outer_rows = exec_scan(t, outer_plan, s.where);

    for (auto& [orid, orow] : outer_rows) {
        if (outer_join_idx < 0 || outer_join_idx >= static_cast<int>(orow.size())) continue;
        const Value& join_key = orow[outer_join_idx];

        if (inner_pk_join) {
            // Index probe on inner table: O(log N) per outer row.
            RID irid;
            if (!inner.index->search(join_key, irid)) continue;
            std::string raw;
            if (!inner.heap->fetch(irid, raw)) continue;
            auto irow = decode(inner.schema, raw);
            if (!row_matches(irow, inner.schema, s.where, inner.name)) continue;
            // Build output row.
            std::vector<Value> out_row = project(orow, t.schema,     s.sel_cols, s.star);
            std::vector<Value> in_row  = project(irow, inner.schema, s.sel_cols, s.star);
            for (auto& v : in_row) out_row.push_back(v);
            res.rows.push_back(out_row);
        } else {
            // Full inner scan: O(N_outer × N_inner).
            Plan inner_plan{ScanType::SEQ, "SeqScan"};
            auto inner_rows = exec_scan(inner, inner_plan, {});
            for (auto& [irid, irow] : inner_rows) {
                if (inner_join_idx < 0 || inner_join_idx >= static_cast<int>(irow.size())) continue;
                if (irow[inner_join_idx] != join_key) continue;
                if (!row_matches(irow, inner.schema, s.where, inner.name)) continue;
                std::vector<Value> out_row = project(orow, t.schema,     s.sel_cols, s.star);
                std::vector<Value> in_row  = project(irow, inner.schema, s.sel_cols, s.star);
                for (auto& v : in_row) out_row.push_back(v);
                res.rows.push_back(out_row);
            }
        }
    }
    return res;
}

// ── DELETE ────────────────────────────────────────────────────────────────────

Result Database::do_delete(const Stmt& s) {
    Table& t = get_table(s.table);
    Plan plan = optimize(t, s.where);
    auto rows = exec_scan(t, plan, s.where);

    if (rows.empty()) return {true, "0 rows deleted."};

    long txnid = cur_txn_ ? cur_txn_->id : next_txn_id_++;
    int  count = 0;

    for (auto& [rid, row] : rows) {
        Value pk_val = row[t.schema.pk];

        if (cur_txn_) {
            try { locks_.acquire(cur_txn_.get(), lock_key(s.table, pk_val), true); }
            catch (DeadlockError&) {
                rollback(cur_txn_.get());
                cur_txn_.reset();
                return {false, "Deadlock detected — transaction aborted."};
            }
        }

        // WAL before modifying data.
        wal_write("DELETE " + std::to_string(txnid) + " " + s.table +
                  " " + pk_val.to_string());

        t.heap->remove(rid);
        t.index->erase(pk_val);
        t.row_count--;
        count++;

        if (cur_txn_) {
            std::string encoded = encode(t.schema, row);
            cur_txn_->undo.push_back({UndoRecord::DELETE, s.table, pk_val, encoded});
        }
    }

    if (!cur_txn_) wal_write("COMMIT " + std::to_string(txnid));

    return {true, std::to_string(count) + " row(s) deleted."};
}

// ── Transaction control ───────────────────────────────────────────────────────

Result Database::do_txn(const Stmt& s) {
    if (s.kind == Kind::BEGIN) {
        if (cur_txn_) return {false, "Already in a transaction."};
        cur_txn_ = std::make_unique<Txn>();
        cur_txn_->id     = next_txn_id_++;
        cur_txn_->active = true;
        wal_write("BEGIN " + std::to_string(cur_txn_->id));
        return {true, "Transaction " + std::to_string(cur_txn_->id) + " started."};
    }

    if (s.kind == Kind::COMMIT) {
        if (!cur_txn_) return {false, "No active transaction."};
        long id = cur_txn_->id;
        wal_write("COMMIT " + std::to_string(id));
        locks_.release_all(cur_txn_.get());
        cur_txn_.reset();
        return {true, "Transaction " + std::to_string(id) + " committed."};
    }

    if (s.kind == Kind::ABORT) {
        if (!cur_txn_) return {false, "No active transaction."};
        long id = cur_txn_->id;
        rollback(cur_txn_.get());
        wal_write("ABORT " + std::to_string(id));
        locks_.release_all(cur_txn_.get());
        cur_txn_.reset();
        return {true, "Transaction " + std::to_string(id) + " aborted."};
    }

    return {false, "Unknown transaction command."};
}

// ── Rollback ──────────────────────────────────────────────────────────────────
// Undo in reverse order: the last operation is undone first.

void Database::rollback(Txn* t) {
    for (int i = static_cast<int>(t->undo.size()) - 1; i >= 0; i--) {
        UndoRecord& u = t->undo[i];
        auto it = tables_.find(u.table);
        if (it == tables_.end()) continue;
        Table& tbl = it->second;

        if (u.op == UndoRecord::INSERT) {
            // Undo an insert → delete the row.
            RID rid;
            if (tbl.index->search(u.key, rid) && rid.valid()) {
                tbl.heap->remove(rid);
                tbl.index->erase(u.key);
                tbl.row_count--;
            }
        } else {
            // Undo a delete → re-insert the row.
            RID rid = tbl.heap->insert(u.encoded_row);
            tbl.index->insert(u.key, rid);
            tbl.row_count++;
        }
    }
    t->undo.clear();
}

// ── Entry point ───────────────────────────────────────────────────────────────

Result Database::execute(const std::string& sql) {
    if (sql.empty()) return {};
    try {
        Stmt s = parse(sql);
        switch (s.kind) {
            case Kind::CREATE: return do_create(s);
            case Kind::INSERT: return do_insert(s);
            case Kind::SELECT: return do_select(s);
            case Kind::DELETE: return do_delete(s);
            case Kind::BEGIN:
            case Kind::COMMIT:
            case Kind::ABORT:  return do_txn(s);
        }
    } catch (const ParseError& e) {
        return {false, std::string("Parse error: ") + e.what()};
    } catch (const std::exception& e) {
        return {false, std::string("Error: ") + e.what()};
    }
    return {false, "Unknown error."};
}

// ── Helpers ───────────────────────────────────────────────────────────────────

Table& Database::get_table(const std::string& name) {
    auto it = tables_.find(name);
    if (it == tables_.end())
        throw std::runtime_error("Table not found: " + name);
    return it->second;
}

std::string Database::lock_key(const std::string& table, const Value& pk) {
    return table + ":" + pk.to_string();
}

// ── Pretty printer ────────────────────────────────────────────────────────────

void print_result(const Result& r) {
    if (!r.ok) { std::cout << "ERROR: " << r.msg << '\n'; return; }

    if (!r.is_query) { std::cout << r.msg << '\n'; return; }

    if (!r.plan_desc.empty() && r.rows.empty() && !r.col_names.empty() == false) {
        // EXPLAIN output
        std::cout << r.msg << '\n';
        return;
    }
    if (!r.msg.empty()) { std::cout << r.msg << '\n'; }

    if (r.col_names.empty() && r.rows.empty()) return;

    // Compute column widths.
    std::vector<size_t> widths(r.col_names.size(), 0);
    for (size_t c = 0; c < r.col_names.size(); c++)
        widths[c] = r.col_names[c].size();
    for (auto& row : r.rows)
        for (size_t c = 0; c < row.size() && c < widths.size(); c++)
            widths[c] = std::max(widths[c], row[c].to_string().size());

    auto sep = [&] {
        std::cout << '+';
        for (size_t w : widths) std::cout << std::string(w + 2, '-') << '+';
        std::cout << '\n';
    };

    sep();
    std::cout << '|';
    for (size_t c = 0; c < r.col_names.size(); c++)
        std::cout << ' ' << std::setw(static_cast<int>(widths[c])) << std::left
                  << r.col_names[c] << " |";
    std::cout << '\n';
    sep();
    for (auto& row : r.rows) {
        std::cout << '|';
        for (size_t c = 0; c < row.size() && c < widths.size(); c++)
            std::cout << ' ' << std::setw(static_cast<int>(widths[c])) << std::left
                      << row[c].to_string() << " |";
        std::cout << '\n';
    }
    sep();
    std::cout << r.rows.size() << " row(s)\n";
}

} // namespace minidb
