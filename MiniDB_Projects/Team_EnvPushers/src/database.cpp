#include "database.hpp"

#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>

#include "catalog/tuple.hpp"
#include "sql/parser.hpp"

namespace minidb {

namespace fs = std::filesystem;

Database::Database(const std::string& dir, size_t buffer_pool_pages)
    : dir_(dir),
      disk_(std::make_unique<DiskManager>((fs::create_directories(dir), dir + "/minidb.db"))),
      bp_(disk_.get(), buffer_pool_pages) {
    catalog_path_ = dir_ + "/minidb.catalog";
    wal_ = std::make_unique<WAL>(dir_ + "/minidb.wal");

    // Write-ahead rule: flush the log before any data page is written back.
    bp_.set_before_flush([this](PageId) { wal_->flush(); });

    catalog_.load(catalog_path_);
    for (const auto& t : catalog_.tables())
        heaps_[t->name] = std::make_unique<HeapFile>(&bp_, t->first_page_id);

    rebuild_indexes();   // populate in-memory PK indexes from the heap
    recover();           // redo committed / undo uncommitted from the WAL
}

Database::~Database() {
    try { checkpoint(); } catch (...) {}
}

// ---- transaction control --------------------------------------------------
Transaction* Database::begin() {
    Transaction* t = txn_mgr_.begin();
    wal_->log_begin(t->id());
    return t;
}
void Database::commit(Transaction* t) {
    wal_->log_commit(t->id());   // durability point (forces the log)
    txn_mgr_.commit(t);
}
void Database::abort(Transaction* t) {
    wal_->log_abort(t->id());
    txn_mgr_.abort(t);           // runs in-memory undo closures + releases locks
}

void Database::checkpoint() {
    bp_.flush_all();
    disk_->sync();
    catalog_.save(catalog_path_);
    wal_->truncate();            // everything durable in the data file now
}

// ---- helpers --------------------------------------------------------------
TableInfo* Database::require_table(const std::string& name) {
    TableInfo* t = catalog_.get_table(name);
    if (!t) throw std::runtime_error("no such table: " + name);
    return t;
}
HeapFile* Database::heap_for(const std::string& name) {
    auto it = heaps_.find(name);
    return it == heaps_.end() ? nullptr : it->second.get();
}
const Value& Database::key_of(TableInfo* t, const Tuple& tuple) {
    int pk = t->schema.primary_key_index();
    return tuple[pk];
}

std::vector<uint8_t> Database::encode_schema(const std::string& name, const Schema& s) {
    std::ostringstream os;
    os << name << ' ' << s.size();
    for (auto& c : s.columns())
        os << ' ' << c.name << ' ' << (c.type == TypeId::INTEGER ? "INT" : "TEXT")
           << ' ' << (c.is_primary_key ? 1 : 0);
    std::string str = os.str();
    return std::vector<uint8_t>(str.begin(), str.end());
}
void Database::decode_schema(const std::vector<uint8_t>& blob, std::string& name, Schema& s) {
    std::string str(blob.begin(), blob.end());
    std::istringstream is(str);
    size_t n;
    is >> name >> n;
    std::vector<Column> cols;
    for (size_t i = 0; i < n; ++i) {
        Column c; std::string ty; int pk;
        is >> c.name >> ty >> pk;
        c.type = (ty == "INT") ? TypeId::INTEGER : TypeId::TEXT;
        c.is_primary_key = (pk != 0);
        cols.push_back(c);
    }
    s = Schema(std::move(cols));
}

void Database::rebuild_indexes() {
    for (const auto& t : catalog_.tables()) {
        if (t->schema.primary_key_index() < 0) continue;
        if (!t->pk_index) t->pk_index = std::make_unique<BPlusTree>();
        HeapFile* heap = heap_for(t->name);
        t->row_count = 0;
        heap->scan([&](const RID& rid, const std::vector<uint8_t>& bytes) {
            Tuple tup = deserialize_tuple(t->schema, bytes);
            t->pk_index->insert(key_of(t.get(), tup), rid);
            t->row_count++;
        });
    }
}

// Insert-or-update a tuple by primary key. Idempotent: safe for recovery redo.
void Database::upsert(TableInfo* t, const Tuple& tuple) {
    HeapFile* heap = heap_for(t->name);
    std::vector<uint8_t> bytes = serialize_tuple(t->schema, tuple);
    const Value& k = key_of(t, tuple);
    auto existing = t->pk_index->search(k);
    if (existing) {
        RID nrid = heap->update(*existing, bytes);
        if (!(nrid == *existing)) t->pk_index->insert(k, nrid);
    } else {
        RID rid = heap->insert(bytes);
        t->pk_index->insert(k, rid);
        t->row_count++;
    }
}

// Delete a tuple by primary key. Idempotent.
void Database::delete_by_key(TableInfo* t, const Value& key) {
    auto rid = t->pk_index->search(key);
    if (!rid) return;
    heap_for(t->name)->erase(*rid);
    t->pk_index->erase(key);
    if (t->row_count > 0) t->row_count--;
}

// ---- recovery -------------------------------------------------------------
void Database::recover() {
    std::vector<LogRecord> log = wal_->read_all();
    if (log.empty()) return;

    std::set<TxnId> committed;
    for (auto& r : log) if (r.type == LogType::COMMIT) committed.insert(r.txn);

    // Redo pass (forward): re-create tables, re-apply every data op idempotently.
    for (auto& r : log) {
        if (r.type == LogType::CREATE_TABLE) {
            std::string name; Schema s;
            decode_schema(r.ddl, name, s);
            if (!catalog_.get_table(name)) {
                PageId fp;
                HeapFile hf = HeapFile::create(&bp_, &fp);
                (void)hf;
                create_table_internal(name, s, fp, /*log_and_persist=*/false);
            }
        } else if (r.type == LogType::INSERT || r.type == LogType::UPDATE) {
            TableInfo* t = catalog_.get_table(r.table);
            if (t) upsert(t, deserialize_tuple(t->schema, r.after));
        } else if (r.type == LogType::DELETE) {
            TableInfo* t = catalog_.get_table(r.table);
            if (t) delete_by_key(t, key_of(t, deserialize_tuple(t->schema, r.before)));
        }
    }

    // Undo pass (backward): reverse operations of transactions that never committed.
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        LogRecord& r = *it;
        if (committed.count(r.txn)) continue;
        TableInfo* t = catalog_.get_table(r.table);
        if (!t) continue;
        if (r.type == LogType::INSERT)
            delete_by_key(t, key_of(t, deserialize_tuple(t->schema, r.after)));
        else if (r.type == LogType::UPDATE)
            upsert(t, deserialize_tuple(t->schema, r.before));
        else if (r.type == LogType::DELETE)
            upsert(t, deserialize_tuple(t->schema, r.before));
    }

    checkpoint();   // fold recovered state into the data file; reset the log
}

// ---- top-level execution --------------------------------------------------
static ResultSet error_result(const std::string& msg) {
    ResultSet r; r.ok = false; r.message = msg; return r;
}

ResultSet Database::execute(const std::string& sql) {
    std::unique_ptr<Statement> stmt;
    try { stmt = Parser(sql).parse(); }
    catch (const std::exception& e) { return error_result(std::string("parse error: ") + e.what()); }

    if (stmt->kind == StmtKind::CREATE_TABLE)
        return exec_create(static_cast<CreateTableStmt*>(stmt.get()));
    if (stmt->kind == StmtKind::BEGIN || stmt->kind == StmtKind::COMMIT ||
        stmt->kind == StmtKind::ABORT)
        return error_result("transaction control is only valid inside a session");

    Transaction* txn = begin();
    try {
        ResultSet r = execute(sql, txn);
        if (r.ok) commit(txn); else abort(txn);
        return r;
    } catch (const DeadlockError& e) {
        abort(txn);
        return error_result(std::string("aborted: ") + e.what());
    } catch (const std::exception& e) {
        abort(txn);
        return error_result(std::string("error: ") + e.what());
    }
}

ResultSet Database::execute(const std::string& sql, Transaction* txn) {
    std::unique_ptr<Statement> stmt = Parser(sql).parse();
    switch (stmt->kind) {
        case StmtKind::CREATE_TABLE:
            return exec_create(static_cast<CreateTableStmt*>(stmt.get()));
        case StmtKind::INSERT:
            return exec_insert(static_cast<InsertStmt*>(stmt.get()), txn);
        case StmtKind::SELECT:
            return exec_select(static_cast<SelectStmt*>(stmt.get()), txn);
        case StmtKind::DELETE:
            return exec_delete(static_cast<DeleteStmt*>(stmt.get()), txn);
        case StmtKind::UPDATE:
            return exec_update(static_cast<UpdateStmt*>(stmt.get()), txn);
        default:
            return error_result("unsupported statement in this context");
    }
}

// ---- CREATE TABLE ---------------------------------------------------------
void Database::create_table_internal(const std::string& name, const Schema& schema,
                                     PageId first_page_id, bool log_and_persist) {
    auto info = std::make_unique<TableInfo>();
    info->table_id = catalog_.next_table_id();
    info->name = name;
    info->schema = schema;
    info->first_page_id = first_page_id;
    if (schema.primary_key_index() >= 0) info->pk_index = std::make_unique<BPlusTree>();
    catalog_.add_table(std::move(info));
    heaps_[name] = std::make_unique<HeapFile>(&bp_, first_page_id);

    if (log_and_persist) {
        wal_->log_create_table(name, encode_schema(name, schema));
        catalog_.save(catalog_path_);
    }
}

ResultSet Database::exec_create(CreateTableStmt* s) {
    if (catalog_.get_table(s->table))
        return error_result("table already exists: " + s->table);
    Schema schema(s->columns);
    if (schema.primary_key_index() < 0)
        return error_result("CREATE TABLE requires a PRIMARY KEY column");
    PageId fp;
    HeapFile::create(&bp_, &fp);
    create_table_internal(s->table, schema, fp, /*log_and_persist=*/true);
    ResultSet r; r.message = "Table '" + s->table + "' created"; return r;
}

// ---- INSERT ---------------------------------------------------------------
ResultSet Database::exec_insert(InsertStmt* s, Transaction* txn) {
    TableInfo* t = require_table(s->table);
    txn_mgr_.locks().acquire(txn->id(), t->table_id, LockMode::EXCLUSIVE);
    const Schema& schema = t->schema;

    size_t inserted = 0;
    for (auto& vals : s->rows) {
        Tuple tuple(schema.size(), Value::Null());
        if (s->columns.empty()) {
            if (vals.size() != schema.size())
                return error_result("INSERT: value count does not match column count");
            for (size_t i = 0; i < vals.size(); ++i) tuple[i] = vals[i];
        } else {
            if (vals.size() != s->columns.size())
                return error_result("INSERT: value count does not match named columns");
            for (size_t i = 0; i < s->columns.size(); ++i) {
                int idx = schema.index_of(s->columns[i]);
                if (idx < 0) return error_result("INSERT: unknown column " + s->columns[i]);
                tuple[idx] = vals[i];
            }
        }
        const Value& k = key_of(t, tuple);
        if (t->pk_index->contains(k))
            return error_result("duplicate primary key: " + k.to_string());

        std::vector<uint8_t> bytes = serialize_tuple(schema, tuple);
        wal_->log_insert(txn->id(), t->name, bytes);   // WAL before data
        RID rid = heap_for(t->name)->insert(bytes);
        t->pk_index->insert(k, rid);
        t->row_count++;

        // Undo for ROLLBACK: remove the row + index entry we just added.
        Value kk = k;
        txn->add_undo([this, t, kk, rid]() {
            heap_for(t->name)->erase(rid);
            t->pk_index->erase(kk);
            if (t->row_count > 0) t->row_count--;
        });
        inserted++;
    }
    ResultSet r; r.message = std::to_string(inserted) + " row(s) inserted"; return r;
}

// ---- SELECT ---------------------------------------------------------------
ResultSet Database::exec_select(SelectStmt* s, Transaction* txn) {
    std::vector<BaseTable> tables;
    auto add_table = [&](const TableRef& ref) {
        TableInfo* t = require_table(ref.name);
        txn_mgr_.locks().acquire(txn->id(), t->table_id, LockMode::SHARED);
        tables.push_back(BaseTable{ref.alias.empty() ? ref.name : ref.alias,
                                   TableAccess{t, heap_for(t->name)}});
    };
    add_table(s->from);
    for (auto& jc : s->joins) add_table(jc.table);

    PhysicalPlan plan = optimizer_.optimize(s, tables);
    ResultSet r;
    r.is_select = true;
    r.explain = plan.explain;
    for (auto& c : plan.root->schema()) r.columns.push_back(c.name);

    plan.root->open();
    while (auto row = plan.root->next()) r.rows.push_back(*row);
    plan.root->close();
    r.message = std::to_string(r.rows.size()) + " row(s)";
    return r;
}

// ---- DELETE ---------------------------------------------------------------
ResultSet Database::exec_delete(DeleteStmt* s, Transaction* txn) {
    TableInfo* t = require_table(s->table);
    txn_mgr_.locks().acquire(txn->id(), t->table_id, LockMode::EXCLUSIVE);
    const Schema& schema = t->schema;

    // Build a one-table schema for predicate evaluation.
    OutSchema osch;
    for (auto& c : schema.columns()) osch.push_back(OutColumn{s->table, c.name, c.type});

    std::vector<std::pair<Value, Tuple>> victims;
    heap_for(t->name)->scan([&](const RID&, const std::vector<uint8_t>& bytes) {
        Tuple tup = deserialize_tuple(schema, bytes);
        if (eval_pred(s->where.get(), tup, osch))
            victims.emplace_back(key_of(t, tup), tup);
    });

    for (auto& [k, tup] : victims) {
        std::vector<uint8_t> before = serialize_tuple(schema, tup);
        wal_->log_delete(txn->id(), t->name, before);
        delete_by_key(t, k);
        Tuple saved = tup;
        txn->add_undo([this, t, saved]() { upsert(t, saved); });
    }
    ResultSet r; r.message = std::to_string(victims.size()) + " row(s) deleted"; return r;
}

// ---- UPDATE ---------------------------------------------------------------
ResultSet Database::exec_update(UpdateStmt* s, Transaction* txn) {
    TableInfo* t = require_table(s->table);
    txn_mgr_.locks().acquire(txn->id(), t->table_id, LockMode::EXCLUSIVE);
    const Schema& schema = t->schema;

    OutSchema osch;
    for (auto& c : schema.columns()) osch.push_back(OutColumn{s->table, c.name, c.type});

    std::vector<Tuple> targets;
    heap_for(t->name)->scan([&](const RID&, const std::vector<uint8_t>& bytes) {
        Tuple tup = deserialize_tuple(schema, bytes);
        if (eval_pred(s->where.get(), tup, osch)) targets.push_back(tup);
    });

    size_t updated = 0;
    for (auto& before_tup : targets) {
        Tuple after = before_tup;
        for (auto& [col, val] : s->assignments) {
            int idx = schema.index_of(col);
            if (idx < 0) return error_result("UPDATE: unknown column " + col);
            after[idx] = val;
        }
        std::vector<uint8_t> before = serialize_tuple(schema, before_tup);
        std::vector<uint8_t> aft = serialize_tuple(schema, after);
        wal_->log_update(txn->id(), t->name, before, aft);

        // If the primary key changed, this is a delete+insert at the key level.
        const Value& old_k = key_of(t, before_tup);
        const Value& new_k = key_of(t, after);
        if (!(old_k == new_k)) delete_by_key(t, old_k);
        upsert(t, after);

        Tuple saved_before = before_tup, saved_after = after;
        txn->add_undo([this, t, saved_before, saved_after]() {
            const Value& ak = key_of(t, saved_after);
            const Value& bk = key_of(t, saved_before);
            if (!(ak == bk)) delete_by_key(t, ak);
            upsert(t, saved_before);
        });
        updated++;
    }
    ResultSet r; r.message = std::to_string(updated) + " row(s) updated"; return r;
}

}  // namespace minidb
