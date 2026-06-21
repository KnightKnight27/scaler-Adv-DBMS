#include "engine.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cmath>

namespace fs = std::filesystem;

// ─── Catalog ──────────────────────────────────────────────────────────────────

void Catalog::add_table(const TableSchema& schema) {
    tables_[schema.table_name] = schema;
}
void Catalog::drop_table(const std::string& name) {
    tables_.erase(name);
}
const TableSchema& Catalog::get(const std::string& name) const {
    auto it = tables_.find(name);
    if (it == tables_.end()) throw std::runtime_error("Table not found: " + name);
    return it->second;
}
bool Catalog::exists(const std::string& name) const { return tables_.count(name) > 0; }
std::vector<std::string> Catalog::table_names() const {
    std::vector<std::string> ns;
    for (auto& [k,v] : tables_) ns.push_back(k);
    return ns;
}

// Persist catalog to a simple text file.  Each line: table_name col_name type pk
void Catalog::save(const std::string& path) const {
    std::ofstream f(path);
    for (auto& [tname, schema] : tables_) {
        for (auto& col : schema.columns) {
            f << tname << ' ' << col.name << ' '
              << (int)col.type << ' ' << (col.primary_key ? 1 : 0) << '\n';
        }
    }
}
void Catalog::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return; // no catalog yet — fresh database
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tname, cname;
        int ttype, pk;
        ss >> tname >> cname >> ttype >> pk;
        ColumnDef col;
        col.name = cname;
        col.type = static_cast<Type>(ttype);
        col.primary_key = (pk == 1);
        TableSchema& schema = tables_[tname];
        schema.table_name = tname;
        schema.columns.push_back(col);
        if (col.primary_key)
            schema.pk_col = static_cast<int>(schema.columns.size()) - 1;
    }
}

// ─── HeapTable ────────────────────────────────────────────────────────────────

HeapTable::HeapTable(const TableSchema& schema, const std::string& db_dir)
    : schema_(schema), db_dir_(db_dir) {
    std::string filepath = db_dir + "/" + schema.table_name + ".db";
    dm_ = std::make_unique<DiskManager>(filepath);
    bp_ = std::make_unique<BufferPool>(*dm_, 64);

    // Rebuild the in-memory index by scanning all existing pages
    for (uint32_t pid = 0; pid < dm_->page_count(); ++pid) {
        Page* page = bp_->fetch(pid);
        for (uint16_t sid = 0; sid < page->num_slots(); ++sid) {
            std::vector<uint8_t> buf;
            if (page->read(sid, buf)) {
                Row row = serde::decode(buf.data(), buf.size(), schema_.types());
                if (schema_.pk_col >= 0)
                    index_.insert(row[schema_.pk_col], {pid, sid});
            }
        }
        bp_->unpin(pid, false);
    }
}

PageId HeapTable::get_or_alloc_write_page() {
    // Try the last existing page first; allocate a new one if full
    if (dm_->page_count() > 0) {
        PageId last = dm_->page_count() - 1;
        Page* page = bp_->fetch(last);
        // We need room for at least a minimal record; 128 B is conservative
        if (page->free_space() >= 128) {
            bp_->unpin(last, false);
            return last;
        }
        bp_->unpin(last, false);
    }
    // Allocate and initialise a fresh page
    PageId new_pid = dm_->allocate_page();
    Page* page = bp_->fetch(new_pid);
    page->init();
    bp_->unpin(new_pid, true);
    return new_pid;
}

RID HeapTable::insert_row(const Row& row) {
    // Enforce the primary-key constraint BEFORE touching a page, otherwise a
    // rejected insert would leave an orphan physical row behind in the heap.
    if (schema_.pk_col >= 0 && index_.search(row[schema_.pk_col]))
        throw std::runtime_error("Duplicate primary key");

    auto bytes = serde::encode(row);
    PageId pid = get_or_alloc_write_page();
    Page* page = bp_->fetch(pid);
    uint16_t sid = page->insert(bytes.data(), static_cast<uint16_t>(bytes.size()));
    if (sid == UINT16_MAX) {
        bp_->unpin(pid, false);
        // Current page is full — allocate a new one
        pid  = dm_->allocate_page();
        page = bp_->fetch(pid);
        page->init();
        sid  = page->insert(bytes.data(), static_cast<uint16_t>(bytes.size()));
    }
    bp_->unpin(pid, true);

    RID rid{pid, sid};
    if (schema_.pk_col >= 0)
        index_.insert(row[schema_.pk_col], rid); // throws on duplicate
    return rid;
}

bool HeapTable::delete_row(RID rid, const Row& row) {
    Page* page = bp_->fetch(rid.page_id);
    page->remove(rid.slot_id);
    bp_->unpin(rid.page_id, true);
    if (schema_.pk_col >= 0)
        index_.remove(row[schema_.pk_col]);
    return true;
}

void HeapTable::scan(std::function<void(RID, const Row&)> visitor) const {
    auto types = schema_.types();
    for (uint32_t pid = 0; pid < dm_->page_count(); ++pid) {
        Page* page = bp_->fetch(pid);
        for (uint16_t sid = 0; sid < page->num_slots(); ++sid) {
            std::vector<uint8_t> buf;
            if (page->read(sid, buf)) {
                Row row = serde::decode(buf.data(), buf.size(), types);
                visitor({pid, sid}, row);
            }
        }
        bp_->unpin(pid, false);
    }
}

Row HeapTable::read_row(RID rid) const {
    Page* page = bp_->fetch(rid.page_id);
    std::vector<uint8_t> buf;
    bool ok = page->read(rid.slot_id, buf);
    bp_->unpin(rid.page_id, false);
    if (!ok) throw std::runtime_error("HeapTable::read_row: slot deleted");
    return serde::decode(buf.data(), buf.size(), schema_.types());
}

std::optional<Row> HeapTable::lookup_by_pk(const Value& pk_val) const {
    auto rid = index_.search(pk_val);
    if (!rid) return std::nullopt;
    return read_row(*rid);
}

std::vector<std::pair<RID, Row>> HeapTable::index_range(const Value& lo, const Value& hi) const {
    auto rids = index_.range_scan(lo, hi);
    std::vector<std::pair<RID, Row>> result;
    for (auto& rid : rids) result.emplace_back(rid, read_row(rid));
    return result;
}

size_t HeapTable::approx_row_count() const {
    // With a primary-key index, every live row has exactly one index entry.
    if (schema_.pk_col >= 0 && !index_.empty())
        return index_.scan_all().size();
    // No index: count live rows by scanning (fine at teaching scale).
    size_t n = 0;
    scan([&](RID, const Row&) { ++n; });
    return n;
}

// ─── Optimizer ────────────────────────────────────────────────────────────────
//
// Cost model (pages read):
//   Table scan   → dm.page_count()          (read everything)
//   Index point  → 1                        (O(log n) + 1 page fetch)
//   Index range  → selectivity * page_count (fraction of pages)
//
// If a WHERE condition is on the primary key column and the operator is EQ,
// we use an index point lookup.  For range operators we use index_range.
// Otherwise we fall back to table scan.
// ─────────────────────────────────────────────────────────────────────────────

double Optimizer::selectivity(const Condition& cond, const TableSchema& schema) const {
    // Rough heuristics — a real optimizer uses statistics/histograms.
    // EQ on PK: very selective (essentially 1 row)
    int col_idx = schema.col_index(cond.left_col);
    bool is_pk  = (col_idx >= 0 && col_idx == schema.pk_col);
    if (cond.op == TokenType::EQ)  return is_pk ? 0.001 : 0.1;
    if (cond.op == TokenType::NEQ) return 0.9;
    // Range operators: assume 25% selectivity
    return 0.25;
}

QueryPlan Optimizer::plan(const TableSchema& schema, const HeapTable& table,
                           bool has_where, const Condition& cond) {
    if (!has_where)
        return {ScanType::TABLE_SCAN, schema.table_name};

    int col_idx = schema.col_index(cond.left_col);
    bool is_pk  = (col_idx >= 0 && col_idx == schema.pk_col);
    bool has_index = is_pk && !table.index().empty();

    if (has_index && !cond.rhs_is_col) {
        if (cond.op == TokenType::EQ)
            return {ScanType::INDEX_POINT, schema.table_name, cond.rhs_val};
        if (cond.op == TokenType::LT || cond.op == TokenType::LTE) {
            Value lo = Value::make_int(INT64_MIN); // open lower bound
            if (schema.columns[col_idx].type == Type::VARCHAR) lo = Value::make_varchar("");
            return {ScanType::INDEX_RANGE, schema.table_name, lo, cond.rhs_val, true};
        }
        if (cond.op == TokenType::GT || cond.op == TokenType::GTE) {
            Value hi = Value::make_int(INT64_MAX); // open upper bound
            if (schema.columns[col_idx].type == Type::VARCHAR) hi = Value::make_varchar("\xFF\xFF");
            return {ScanType::INDEX_RANGE, schema.table_name, cond.rhs_val, hi, true};
        }
    }
    return {ScanType::TABLE_SCAN, schema.table_name};
}

size_t Optimizer::estimate_cardinality(const TableSchema& schema, const HeapTable& table,
                                       bool has_where, const Condition& cond) const {
    size_t base = table.approx_row_count();
    if (!has_where) return base;
    double est = base * selectivity(cond, schema);
    return est < 1.0 ? 1 : static_cast<size_t>(est);
}

JoinPlan Optimizer::plan_join(const TableSchema& from_s, const HeapTable& from_t,
                              bool from_has_where, const Condition& where,
                              const TableSchema& join_s, const HeapTable& join_t) {
    size_t from_rows = estimate_cardinality(from_s, from_t, from_has_where, where);
    size_t join_rows = estimate_cardinality(join_s, join_t, false, where);
    // Stream the larger relation, materialise the smaller one as the inner side
    // of the nested-loop join. (Equal sizes → keep FROM as outer, deterministic.)
    bool from_is_outer = from_rows >= join_rows;
    return {from_is_outer, from_rows, join_rows};
}

// ─── Executor helpers ────────────────────────────────────────────────────────

Value Executor::get_value_from_row(const std::string& col, const std::string& /*tbl_hint*/,
                                    const TableSchema& schema, const Row& row) const {
    int i = schema.col_index(col);
    if (i < 0) throw std::runtime_error("Column not found: " + col);
    return row[i];
}

// Resolve a (table-qualifier, column) reference to a Value, given up to two
// (schema, row) pairs. `prefer_second` decides which table to try first when
// the reference is unqualified and the column name exists in both tables — used
// so that an unqualified JOIN condition like `did = did` resolves its right-hand
// side to the RIGHT table instead of comparing the left table against itself.
static bool resolve_ref(const std::string& tbl, const std::string& col,
                        const TableSchema& s1, const Row& r1,
                        const TableSchema* s2, const Row* r2,
                        bool prefer_second, Value& out) {
    auto try_one = [&](const TableSchema& sc, const Row& rw) -> bool {
        if (!tbl.empty() && tbl != sc.table_name) return false; // qualifier mismatch
        int i = sc.col_index(col);
        if (i < 0) return false;
        out = rw[i];
        return true;
    };
    if (prefer_second && s2 && r2) { if (try_one(*s2, *r2)) return true; }
    if (try_one(s1, r1)) return true;
    if (s2 && r2) { if (try_one(*s2, *r2)) return true; }
    return false;
}

bool Executor::eval_cond(const Condition& c, const TableSchema& s, const Row& row,
                          const TableSchema* s2, const Row* row2) const {
    Value lhs, rhs;
    // Left side: a column reference, qualifier honoured, defaults to the left table.
    if (!resolve_ref(c.left_table, c.left_col, s, row, s2, row2,
                     /*prefer_second=*/false, lhs))
        throw std::runtime_error("Column not found: " + c.left_col);

    if (c.rhs_is_col) {
        // Right side defaults to the OTHER table when unqualified (join semantics).
        if (!resolve_ref(c.rhs_table, c.rhs_col, s, row, s2, row2,
                         /*prefer_second=*/true, rhs))
            throw std::runtime_error("Column not found: " + c.rhs_col);
    } else {
        rhs = c.rhs_val;
    }

    switch (c.op) {
        case TokenType::EQ:  return lhs == rhs;
        case TokenType::NEQ: return lhs != rhs;
        case TokenType::LT:  return lhs <  rhs;
        case TokenType::LTE: return lhs <= rhs;
        case TokenType::GT:  return lhs >  rhs;
        case TokenType::GTE: return lhs >= rhs;
        default: return false;
    }
}

// ─── Executor::execute_select ─────────────────────────────────────────────────

ResultSet Executor::execute_select(const SelectStmt& s) {
    auto& schema = catalog_.get(s.table);
    auto& tbl    = *tables_.at(s.table);

    // Build result column list
    ResultSet rs;
    if (s.star) for (auto& c : schema.columns) rs.columns.push_back(c.name);
    else        rs.columns = s.columns;

    QueryPlan plan = opt_.plan(schema, tbl, s.has_where, s.where_cond);

    // Collect matching rows from the left table
    std::vector<std::pair<RID, Row>> left_rows;

    if (plan.scan == ScanType::INDEX_POINT && !s.has_join) {
        auto row = tbl.lookup_by_pk(plan.lo);
        if (row) left_rows.emplace_back(RID{}, *row);
    } else if (plan.scan == ScanType::INDEX_RANGE && !s.has_join) {
        left_rows = tbl.index_range(plan.lo, plan.hi);
    } else {
        tbl.scan([&](RID rid, const Row& row) {
            if (!s.has_where || eval_cond(s.where_cond, schema, row))
                left_rows.emplace_back(rid, row);
        });
    }

    if (!s.has_join) {
        for (auto& [rid, row] : left_rows) {
            Row out;
            for (auto& cn : rs.columns) {
                int i = schema.col_index(cn);
                if (i < 0) throw std::runtime_error("Column not found: " + cn);
                out.push_back(row[i]);
            }
            rs.rows.push_back(std::move(out));
        }
        return rs;
    }

    // ── JOIN: nested-loop join ────────────────────────────────────────────
    // `left_rows` already holds the FROM table filtered by WHERE. We also
    // materialise the JOIN table, then let the optimizer pick the join order:
    // the smaller relation becomes the inner (build) side, the larger the outer.
    auto& rschema = catalog_.get(s.join_table);
    auto& rtbl    = *tables_.at(s.join_table);

    std::vector<Row> join_rows;
    rtbl.scan([&](RID, const Row& row) { join_rows.push_back(row); });

    JoinPlan jp = opt_.plan_join(schema, tbl, s.has_where, s.where_cond, rschema, rtbl);

    // Output columns are always FROM-table columns first, then JOIN-table
    // columns, regardless of which relation we physically stream.
    if (s.star) for (auto& c : rschema.columns) rs.columns.push_back(c.name);

    // Emit one combined result row from a (FROM row, JOIN row) match.
    auto emit = [&](const Row& frow, const Row& jrow) {
        if (s.star) {
            Row out = frow;
            out.insert(out.end(), jrow.begin(), jrow.end());
            rs.rows.push_back(std::move(out));
            return;
        }
        Row out;
        for (auto& cn : rs.columns) {
            int i = schema.col_index(cn);
            if (i >= 0) { out.push_back(frow[i]); continue; }
            i = rschema.col_index(cn);
            if (i >= 0) { out.push_back(jrow[i]); continue; }
            throw std::runtime_error("Column not found: " + cn);
        }
        rs.rows.push_back(std::move(out));
    };

    // Always evaluate the join condition with (FROM schema, JOIN schema) so
    // qualifier resolution is independent of the chosen loop order.
    if (jp.from_is_outer) {
        for (auto& [lid, frow] : left_rows)
            for (auto& jrow : join_rows)
                if (eval_cond(s.join_cond, schema, frow, &rschema, &jrow))
                    emit(frow, jrow);
    } else {
        for (auto& jrow : join_rows)
            for (auto& [lid, frow] : left_rows)
                if (eval_cond(s.join_cond, schema, frow, &rschema, &jrow))
                    emit(frow, jrow);
    }
    return rs;
}

// ─── Executor::execute_insert ─────────────────────────────────────────────────

RID Executor::execute_insert(const InsertStmt& s) {
    auto& schema = catalog_.get(s.table);
    if (s.values.size() != schema.columns.size())
        throw std::runtime_error("INSERT column count mismatch");
    return tables_.at(s.table)->insert_row(s.values);
}

// ─── Executor::execute_delete ─────────────────────────────────────────────────

std::vector<std::pair<RID, Row>> Executor::execute_delete(const DeleteStmt& s) {
    auto& schema = catalog_.get(s.table);
    auto& tbl    = *tables_.at(s.table);
    std::vector<std::pair<RID, Row>> to_delete;
    tbl.scan([&](RID rid, const Row& row) {
        if (!s.has_where || eval_cond(s.where_cond, schema, row))
            to_delete.emplace_back(rid, row);
    });
    for (auto& [rid, row] : to_delete) tbl.delete_row(rid, row);
    return to_delete; // before-images for undo logging
}

// ─── Executor::execute_create ─────────────────────────────────────────────────

void Executor::execute_create(const CreateStmt& s, const std::string& db_dir) {
    if (catalog_.exists(s.table))
        throw std::runtime_error("Table already exists: " + s.table);
    TableSchema schema;
    schema.table_name = s.table;
    for (auto& c : s.cols) {
        ColumnDef cd{c.name, c.type, c.primary_key};
        if (c.primary_key) schema.pk_col = static_cast<int>(schema.columns.size());
        schema.columns.push_back(cd);
    }
    catalog_.add_table(schema);
    tables_[s.table] = std::make_unique<HeapTable>(schema, db_dir);
}

void Executor::execute_drop(const DropStmt& s) {
    catalog_.drop_table(s.table);
    tables_.erase(s.table);
}

// ─── Database ────────────────────────────────────────────────────────────────

Database::Database(const std::string& db_dir) : db_dir_(db_dir) {
    fs::create_directories(db_dir);
    // Load persisted schema
    catalog_.load(db_dir + "/catalog.cat");
    // Open heap tables for every known schema
    for (auto& tname : catalog_.table_names())
        tables_[tname] = std::make_unique<HeapTable>(catalog_.get(tname), db_dir);
    executor_ = std::make_unique<Executor>(catalog_, tables_);
}

ResultSet Database::execute(const std::string& sql) {
    Parser parser(sql);
    Statement stmt = parser.parse();
    return std::visit([&](auto& s) -> ResultSet {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, SelectStmt>) {
            return executor_->execute_select(s);
        } else if constexpr (std::is_same_v<T, InsertStmt>) {
            executor_->execute_insert(s);
            catalog_.save(db_dir_ + "/catalog.cat");
            return {};
        } else if constexpr (std::is_same_v<T, DeleteStmt>) {
            executor_->execute_delete(s);
            return {};
        } else if constexpr (std::is_same_v<T, CreateStmt>) {
            executor_->execute_create(s, db_dir_);
            catalog_.save(db_dir_ + "/catalog.cat");
            return {};
        } else if constexpr (std::is_same_v<T, DropStmt>) {
            executor_->execute_drop(s);
            catalog_.save(db_dir_ + "/catalog.cat");
            return {};
        } else {
            // BEGIN / COMMIT / ROLLBACK handled by TransactionManager layer
            return {};
        }
    }, stmt);
}

// Executor constructor (defined here to avoid circular header issues)
Executor::Executor(Catalog& catalog,
                   std::map<std::string, std::unique_ptr<HeapTable>>& tables)
    : catalog_(catalog), tables_(tables) {}
