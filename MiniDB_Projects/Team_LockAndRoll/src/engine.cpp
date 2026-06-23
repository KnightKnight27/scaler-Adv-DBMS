#include "engine.h"

#include <algorithm>
#include <cctype>

#include "execution.h"
#include "optimizer.h"

namespace minidb {

Database::Database(const std::string& dir, CCMode mode, size_t pool_frames)
    : dir_(dir),
      mode_(mode),
      dm_(dir_),
      bpool_(&dm_, pool_frames),
      log_(dir_ + "/wal.log"),
      catalog_(&dm_, &bpool_, &log_, dir_),
      txnmgr_(mode) {
  bpool_.set_log_flusher(&log_);
  catalog_.load();
  RecoveryManager(&bpool_, &log_).recover();  // replay the WAL
  rebuild_memory_state();
}

Database::~Database() {
  bpool_.flush_all();
  dm_.sync();
  log_.flush();
}

void Database::rebuild_memory_state() {
  vstore_.clear();
  for (TableInfo* t : catalog_.tables()) {
    t->index = std::make_unique<BPlusTree>();
    t->heap->scan([&](RID rid, const Tuple& tup) {
      int64_t pk = t->pk_of(tup);
      t->index->insert(pk, rid);
      if (mode_ == CCMode::MVCC) vstore_.put_base(t->oid, pk, tup, rid);
      return true;
    });
  }
}

TableInfo* Database::table_by_oid(int oid) const {
  for (TableInfo* t : catalog_.tables())
    if (t->oid == oid) return t;
  throw DBException("unknown table oid");
}

RID Database::apply_committed(txn_id_t txn, int oid, int64_t key, bool deleted,
                              const Tuple& tuple) {
  TableInfo* t = table_by_oid(oid);
  std::unique_lock<std::shared_mutex> lk(t->mu);
  if (deleted) {
    auto rid = t->index->search(key);
    if (rid) {
      t->heap->remove(txn, *rid);
      t->index->erase(key);
    }
    return RID{};
  }
  auto existing = t->index->search(key);  // an MVCC update replaces in place
  if (existing) {
    t->heap->remove(txn, *existing);
    t->index->erase(key);
  }
  RID rid = t->heap->insert(txn, tuple);
  t->index->insert(key, rid);
  return rid;
}

void Database::commit(Transaction* txn) {
  if (txn->mode == CCMode::MVCC) {
    if (!txn->mvcc_keys.empty()) {
      // the commit ts is assigned inside vstore_.commit, atomically with publishing the versions
      vstore_.commit(txn, [&](int oid, int64_t key, bool del, const Tuple& tup) {
        return apply_committed(txn->id, oid, key, del, tup);
      });
      log_.log_commit(txn->id);  // durable only if the txn actually wrote
    }
  } else {
    // read-only txn: nothing to make durable, skip the commit fsync, just drop read locks
    if (!txn->undo.empty()) log_.log_commit(txn->id);
    lockmgr_.release_all(txn->id);
  }
  txn->state = TxnState::COMMITTED;
  txnmgr_.end(txn);
}

void Database::abort(Transaction* txn) {
  if (txn->mode == CCMode::MVCC) {
    vstore_.abort(txn);
    log_.log_abort(txn->id);
  } else {
    // undo logical ops newest-first
    for (auto it = txn->undo.rbegin(); it != txn->undo.rend(); ++it) {
      TableInfo* t = table_by_oid(it->table_oid);
      std::unique_lock<std::shared_mutex> lk(t->mu);
      if (it->was_insert) {
        t->heap->remove(txn->id, it->rid);
        t->index->erase(it->key);
      } else {
        RID nr = t->heap->insert(txn->id, it->tuple);
        t->index->insert(it->key, nr);
      }
    }
    log_.log_abort(txn->id);
    lockmgr_.release_all(txn->id);
  }
  txnmgr_.record_abort();
  txn->state = TxnState::ABORTED;
  txnmgr_.end(txn);
}

bool Database::read_key(Transaction* txn, TableInfo* t, int64_t key, Tuple* out) {
  if (txn->mode == CCMode::MVCC)
    return vstore_.read(t->oid, key, txn->start_ts, txn->id, out);
  lockmgr_.acquire(txn->id, {t->oid, key}, LockManager::Mode::SHARED);
  std::shared_lock<std::shared_mutex> lk(t->mu);
  auto rid = t->index->search(key);
  if (!rid) return false;
  return t->heap->get(*rid, out);
}

void Database::scan_table(Transaction* txn, TableInfo* t,
                          const std::function<bool(RID, const Tuple&)>& fn) {
  if (txn->mode == CCMode::MVCC) {
    // mvcc reads come from versions; the heap RID may be stale, so don't hand it out
    vstore_.scan(t->oid, txn->start_ts, txn->id,
                 [&](int64_t, const Tuple& tup, RID) { fn(RID{}, tup); });
    return;
  }
  // strict 2PL: snapshot row identities under the latch, then lock each row before
  // reading it and re-read under the latch, so the value returned is the locked one
  std::vector<std::pair<RID, int64_t>> ids;  // (rid, primary key)
  {
    std::shared_lock<std::shared_mutex> lk(t->mu);
    t->heap->scan([&](RID rid, const Tuple& tup) {
      ids.emplace_back(rid, t->pk_of(tup));
      return true;
    });
  }
  for (auto& [rid, pk] : ids) {
    lockmgr_.acquire(txn->id, {t->oid, pk}, LockManager::Mode::SHARED);
    Tuple cur;
    bool live;
    {
      std::shared_lock<std::shared_mutex> lk(t->mu);
      live = t->heap->get(rid, &cur);
    }
    // skip if the row was deleted or the slot got reused before we held the lock
    if (!live || t->pk_of(cur) != pk) continue;
    if (!fn(rid, cur)) break;
  }
}

void Database::insert_row(Transaction* txn, TableInfo* t, const Tuple& tuple) {
  int64_t pk = t->pk_of(tuple);
  if (txn->mode == CCMode::MVCC) {
    Tuple tmp;
    if (vstore_.read(t->oid, pk, txn->start_ts, txn->id, &tmp))
      throw DBException("duplicate primary key: " + std::to_string(pk));
    if (!vstore_.write(t->oid, pk, txn->id, txn->start_ts, false, tuple))
      throw AbortException("write-write conflict on key " + std::to_string(pk));
    txn->mvcc_keys.push_back({t->oid, pk});
    return;
  }
  lockmgr_.acquire(txn->id, {t->oid, pk}, LockManager::Mode::EXCLUSIVE);
  std::unique_lock<std::shared_mutex> lk(t->mu);
  if (t->index->search(pk)) throw DBException("duplicate primary key: " + std::to_string(pk));
  RID rid = t->heap->insert(txn->id, tuple);
  t->index->insert(pk, rid);
  txn->undo.push_back({true, t->oid, pk, rid, tuple});
}

size_t Database::delete_row(Transaction* txn, TableInfo* t, int64_t key) {
  if (txn->mode == CCMode::MVCC) {
    Tuple cur;
    if (!vstore_.read(t->oid, key, txn->start_ts, txn->id, &cur)) return 0;
    if (!vstore_.write(t->oid, key, txn->id, txn->start_ts, true, cur))
      throw AbortException("write-write conflict on key " + std::to_string(key));
    txn->mvcc_keys.push_back({t->oid, key});
    return 1;
  }
  lockmgr_.acquire(txn->id, {t->oid, key}, LockManager::Mode::EXCLUSIVE);
  std::unique_lock<std::shared_mutex> lk(t->mu);
  auto rid = t->index->search(key);
  if (!rid) return 0;
  Tuple old;
  if (!t->heap->get(*rid, &old)) return 0;
  t->heap->remove(txn->id, *rid);
  t->index->erase(key);
  txn->undo.push_back({false, t->oid, key, *rid, old});
  return 1;
}

void Database::checkpoint() {
  bpool_.flush_all();
  dm_.sync();
  log_.flush();
}

void Database::simulate_crash_and_recover() {
  current_txn_ = nullptr;
  lockmgr_.reset();  // the lock table is volatile, wouldn't survive a real crash
  bpool_.reset_discard();
  RecoveryManager(&bpool_, &log_).recover();
  rebuild_memory_state();
}

namespace {
std::string ltrim_upper_word(const std::string& s) {
  size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
  size_t j = i;
  while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j]))) j++;
  std::string w = s.substr(i, j - i);
  for (char& c : w) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return w;
}
}  // namespace

ResultSet Database::execute(const std::string& sql) {
  // EXPLAIN <select>: show the plan without running it
  if (ltrim_upper_word(sql) == "EXPLAIN") {
    size_t p = sql.find_first_not_of(" \t\n\r");
    size_t sp = sql.find_first_of(" \t\n\r", p);
    if (sp == std::string::npos) throw DBException("EXPLAIN requires a SELECT statement");
    std::string rest = sql.substr(sp + 1);
    Statement st = Parser::parse(rest);
    if (!std::holds_alternative<SelectStmt>(st))
      throw DBException("EXPLAIN supports SELECT only");
    Optimizer opt(&catalog_);
    PlanPtr plan = opt.build(std::get<SelectStmt>(st));
    ResultSet r;
    r.message = "Query plan:\n" + Optimizer::explain(plan);
    return r;
  }

  Statement st = Parser::parse(sql);

  if (std::holds_alternative<CreateTableStmt>(st))
    return exec_create(std::get<CreateTableStmt>(st));

  if (std::holds_alternative<TxnStmt>(st)) {
    TxnStmt& ts = std::get<TxnStmt>(st);
    ResultSet r;
    if (ts.cmd == TxnCmd::Begin) {
      if (current_txn_) throw DBException("already in a transaction");
      current_txn_ = begin(false);
      r.message = "BEGIN";
    } else if (ts.cmd == TxnCmd::Commit) {
      if (!current_txn_) throw DBException("no active transaction");
      commit(current_txn_);
      current_txn_ = nullptr;
      r.message = "COMMIT";
    } else {
      if (!current_txn_) throw DBException("no active transaction");
      abort(current_txn_);
      current_txn_ = nullptr;
      r.message = "ROLLBACK";
    }
    return r;
  }

  // DML: run in the explicit txn, or a fresh autocommit one
  bool autoc = (current_txn_ == nullptr);
  Transaction* txn = autoc ? begin(true) : current_txn_;
  try {
    ResultSet r;
    if (std::holds_alternative<InsertStmt>(st))
      r = exec_insert(std::get<InsertStmt>(st), txn);
    else if (std::holds_alternative<SelectStmt>(st))
      r = exec_select(std::get<SelectStmt>(st), txn);
    else if (std::holds_alternative<DeleteStmt>(st))
      r = exec_delete(std::get<DeleteStmt>(st), txn);
    if (autoc) commit(txn);
    return r;
  } catch (...) {
    if (autoc)
      abort(txn);
    else {
      abort(current_txn_);  // an error rolls back the explicit txn
      current_txn_ = nullptr;
    }
    throw;
  }
}

ResultSet Database::exec_create(const CreateTableStmt& s) {
  std::vector<Column> cols;
  int pk = -1;
  for (size_t i = 0; i < s.columns.size(); i++) {
    cols.push_back({s.columns[i].name, s.columns[i].type});
    if (s.columns[i].primary_key) {
      if (pk >= 0) throw DBException("multiple PRIMARY KEY columns");
      pk = static_cast<int>(i);
    }
  }
  if (pk < 0) throw DBException("table must declare one INTEGER PRIMARY KEY");
  catalog_.create_table(s.table, Schema(std::move(cols)), pk);
  ResultSet r;
  r.message = "CREATE TABLE " + s.table;
  return r;
}

ResultSet Database::exec_insert(const InsertStmt& s, Transaction* txn) {
  TableInfo* t = catalog_.get_table(s.table);
  if (!t) throw DBException("no such table: " + s.table);
  const Schema& schema = t->schema;
  Schema empty;
  Row no_row;
  ResultSet r;
  for (const auto& vals : s.rows) {
    std::vector<Value> tuple_vals(schema.size(), Value::null());
    if (s.columns.empty()) {
      if (vals.size() != schema.size())
        throw DBException("column count mismatch in INSERT");
      for (size_t i = 0; i < vals.size(); i++)
        tuple_vals[i] = eval_expr(vals[i].get(), empty, no_row);
    } else {
      if (vals.size() != s.columns.size())
        throw DBException("column/value count mismatch in INSERT");
      for (size_t i = 0; i < s.columns.size(); i++) {
        int idx = schema.index_of(s.columns[i]);
        if (idx < 0) throw DBException("no such column: " + s.columns[i]);
        tuple_vals[idx] = eval_expr(vals[i].get(), empty, no_row);
      }
    }
    for (size_t i = 0; i < schema.size(); i++) {
      const Value& v = tuple_vals[i];
      if (!v.is_null() && v.type() != schema.column(i).type)
        throw DBException("type mismatch for column " + schema.column(i).name);
    }
    // pk backs the index and the row identity used by concurrency control, so it can't be null
    if (tuple_vals[t->pk_index].is_null())
      throw DBException("primary key '" + schema.column(t->pk_index).name +
                        "' cannot be NULL");
    insert_row(txn, t, Tuple(std::move(tuple_vals)));
    r.affected++;
  }
  r.message = "INSERT " + std::to_string(r.affected);
  return r;
}

ResultSet Database::exec_select(const SelectStmt& s, Transaction* txn) {
  Optimizer opt(&catalog_);
  PlanPtr plan = opt.build(s);
  auto exec = build_executor(plan, ExecutionContext{this, txn});
  exec->open();

  ResultSet r;
  r.is_query = true;
  for (const Column& c : exec->out_schema().columns()) r.columns.push_back(c.name);

  Row row;
  while (exec->next(&row)) r.rows.push_back(row);

  if (!s.order_by.empty()) {
    std::vector<std::pair<int, bool>> keys;  // (output column index, desc)
    for (const auto& ob : s.order_by) {
      int idx = exec->out_schema().index_of(ob.col);
      if (idx < 0) throw DBException("ORDER BY: unknown column " + ob.col);
      keys.emplace_back(idx, ob.desc);
    }
    std::stable_sort(r.rows.begin(), r.rows.end(),
                     [&](const Row& a, const Row& b) {
                       for (auto& [idx, desc] : keys) {
                         int c = a[idx].compare(b[idx]);
                         if (c != 0) return desc ? c > 0 : c < 0;
                       }
                       return false;
                     });
  }

  if (s.has_limit && static_cast<int64_t>(r.rows.size()) > s.limit)
    r.rows.resize(std::max<int64_t>(0, s.limit));

  return r;
}

ResultSet Database::exec_delete(const DeleteStmt& s, Transaction* txn) {
  TableInfo* t = catalog_.get_table(s.table);
  if (!t) throw DBException("no such table: " + s.table);
  Schema qschema = [&] {
    std::vector<Column> cols;
    for (const Column& c : t->schema.columns()) cols.push_back({t->name + "." + c.name, c.type});
    return Schema(std::move(cols));
  }();

  std::vector<int64_t> keys;
  scan_table(txn, t, [&](RID, const Tuple& tup) {
    bool match = true;
    if (s.where) {
      Value v = eval_expr(s.where.get(), qschema, tup.values());
      match = v.is_bool() && v.as_bool();
    }
    if (match) keys.push_back(t->pk_of(tup));
    return true;
  });

  ResultSet r;
  for (int64_t k : keys) r.affected += delete_row(txn, t, k);
  r.message = "DELETE " + std::to_string(r.affected);
  return r;
}

}  // namespace minidb
