#include "engine/database.h"
#include <set>
#include <stdexcept>
#include "exec/executor.h"
#include "storage/heap_row_store.h"
#include "storage/lsm_row_store.h"

namespace minidb {

Database::Database(const std::string &name)
    : name_(name),
      disk_(std::make_unique<DiskManager>(name + ".db")),
      bpm_(std::make_unique<BufferPoolManager>(disk_.get())),
      catalog_(name + ".catalog"),
      optimizer_(this),
      txn_mgr_(&lock_mgr_),
      wal_(name + ".wal") {
  // Eagerly open every table recorded in the catalog so reopened databases work.
  for (const std::string &t : catalog_.TableNames()) {
    OpenStore(catalog_.GetTable(t));
  }
  Recover();  // replay the WAL: redo committed work, undo the rest
}

Database::~Database() { Flush(); }

void Database::Flush() {
  for (auto &kv : stores_) kv.second->Sync();  // flush LSM MemTables, etc.
  if (bpm_) bpm_->FlushAll();
  catalog_.Save();
}

RowStore *Database::OpenStore(TableInfo *info) {
  if (info->storage == StorageType::LSM) {
    auto store = std::make_unique<LSMRowStore>(info->lsm_dir, info->schema);
    RowStore *raw = store.get();
    stores_[info->name] = std::move(store);
    return raw;
  }
  page_id_t heap = info->heap_first_page;
  page_id_t idx = info->index_root_page;
  auto store = std::make_unique<HeapRowStore>(bpm_.get(), info->schema, &heap, &idx);
  if (heap != info->heap_first_page || idx != info->index_root_page) {
    info->heap_first_page = heap;
    info->index_root_page = idx;
    catalog_.Save();
  }
  RowStore *raw = store.get();
  stores_[info->name] = std::move(store);
  return raw;
}

RowStore *Database::GetStore(const std::string &table) {
  auto it = stores_.find(table);
  if (it != stores_.end()) return it->second.get();
  TableInfo *info = catalog_.GetTable(table);
  if (!info) throw std::runtime_error("no such table: " + table);
  return OpenStore(info);
}

ExecResult Database::DoCreate(const CreateTableStmt &s) {
  if (catalog_.HasTable(s.table)) {
    return {false, false, "table already exists: " + s.table, {}, {}, ""};
  }
  TableInfo info;
  info.name = s.table;
  info.schema = Schema(s.columns);
  info.storage = s.storage;
  if (info.schema.pk_index() < 0) {
    return {false, false, "table must declare a PRIMARY KEY", {}, {}, ""};
  }
  if (info.storage == StorageType::LSM) info.lsm_dir = name_ + "_lsm_" + s.table;
  TableInfo *ti = catalog_.CreateTable(info);
  OpenStore(ti);
  // Make the table's initial (empty) pages durable so a crash before any data
  // flush still leaves a well-formed heap/index to recover into.
  bpm_->FlushAll();
  return {true, false, "created table " + s.table, {}, {}, ""};
}

ExecResult Database::DoInsert(const InsertStmt &s) {
  RowStore *store = GetStore(s.table);
  const Schema &sch = store->schema();
  if (s.values.size() != sch.num_columns()) {
    return {false, false, "column count mismatch", {}, {}, ""};
  }
  std::vector<Value> vals;
  for (size_t i = 0; i < sch.num_columns(); ++i) {
    if (sch.column(i).type != s.values[i].type) {
      return {false, false, "type mismatch in column '" + sch.column(i).name + "'", {}, {}, ""};
    }
    vals.push_back(s.values[i]);
  }
  Tuple row(std::move(vals));
  int64_t key = row.value(sch.pk_index()).i;

  StmtTxn stx = StmtBegin();
  try {
    lock_mgr_.LockExclusive(stx.txn, RowId{s.table, key});      // strict 2PL
    wal_.Append({0, stx.txn->id(), LogType::INSERT, s.table, key, row.Serialize(sch)});  // write-ahead
    store->Insert(key, row);
    stx.txn->PushUndo([store, key] { store->Delete(key); });    // for ROLLBACK
    StmtCommit(stx);
    return {true, false, "1 row inserted", {}, {}, ""};
  } catch (...) {
    StmtAbort(stx);
    throw;
  }
}

ExecResult Database::DoSelect(const SelectStmt &s) {
  PlanResult plan = optimizer_.PlanSelect(s);
  ExecResult r;
  r.is_query = true;
  r.explain = plan.explain;
  const Schema &out = plan.root->OutputSchema();
  for (size_t i = 0; i < out.num_columns(); ++i) r.columns.push_back(out.column(i).name);

  plan.root->Init();
  Tuple t;
  while (plan.root->Next(&t)) {
    std::vector<std::string> row;
    for (size_t i = 0; i < t.size(); ++i) row.push_back(t.value(i).ToString());
    r.rows.push_back(std::move(row));
  }
  r.message = std::to_string(r.rows.size()) + " row(s)";
  return r;
}

ExecResult Database::DoDelete(const DeleteStmt &s) {
  RowStore *store = GetStore(s.table);
  const Schema &base = store->schema();
  Schema qualified = QualifySchema(base, s.table);
  std::vector<BoundPredicate> preds = BindPredicates(qualified, s.where);
  int pk = base.pk_index();

  // Collect matching (key,row) first (don't mutate while scanning), then delete.
  std::vector<std::pair<int64_t, Tuple>> victims;
  auto cursor = store->ScanAll();
  Tuple t;
  while (cursor->Next(&t)) {
    if (preds.empty() || EvalPredicates(t, preds)) victims.emplace_back(t.value(pk).i, t);
  }

  StmtTxn stx = StmtBegin();
  try {
    for (auto &v : victims) {
      int64_t k = v.first;
      Tuple old_row = v.second;
      lock_mgr_.LockExclusive(stx.txn, RowId{s.table, k});
      wal_.Append({0, stx.txn->id(), LogType::DELETE, s.table, k, old_row.Serialize(base)});
      store->Delete(k);
      stx.txn->PushUndo([store, k, old_row] { store->Insert(k, old_row); });
    }
    StmtCommit(stx);
    return {true, false, std::to_string(victims.size()) + " row(s) deleted", {}, {}, ""};
  } catch (...) {
    StmtAbort(stx);
    throw;
  }
}

Database::StmtTxn Database::StmtBegin() {
  if (current_txn_) return {current_txn_, false};  // inside an explicit BEGIN
  Transaction *t = txn_mgr_.Begin();
  wal_.Append({0, t->id(), LogType::BEGIN, "", 0, ""});
  return {t, true};
}

void Database::StmtCommit(StmtTxn s) {
  if (!s.implicit) return;  // explicit txn commits on COMMIT
  wal_.Append({0, s.txn->id(), LogType::COMMIT, "", 0, ""});
  txn_mgr_.Commit(s.txn);
}

void Database::StmtAbort(StmtTxn s) {
  if (!s.implicit) return;
  txn_mgr_.Abort(s.txn);  // replays undo log
  wal_.Append({0, s.txn->id(), LogType::ABORT, "", 0, ""});
}

void Database::Recover() {
  std::vector<LogRecord> recs = wal_.ReadAll();
  if (recs.empty()) return;

  // A transaction's effects survive only if it has a COMMIT record.
  std::set<txn_id_t> committed;
  for (const LogRecord &r : recs) {
    if (r.type == LogType::COMMIT) committed.insert(r.txn);
  }

  auto store_for = [&](const std::string &tbl) -> RowStore * {
    try { return GetStore(tbl); } catch (...) { return nullptr; }
  };

  // REDO pass (forward): reapply committed inserts/deletes, idempotently.
  for (const LogRecord &r : recs) {
    if (!committed.count(r.txn)) continue;
    RowStore *st = store_for(r.table);
    if (!st) continue;
    Tuple probe;
    if (r.type == LogType::INSERT) {
      if (!st->Get(r.key, &probe)) st->Insert(r.key, Tuple::Deserialize(st->schema(), r.image));
    } else if (r.type == LogType::DELETE) {
      if (st->Get(r.key, &probe)) st->Delete(r.key);
    }
  }
  // UNDO pass (backward): revert effects of uncommitted transactions.
  for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
    const LogRecord &r = *it;
    if (committed.count(r.txn)) continue;
    RowStore *st = store_for(r.table);
    if (!st) continue;
    Tuple probe;
    if (r.type == LogType::INSERT) {
      if (st->Get(r.key, &probe)) st->Delete(r.key);  // undo an insert
    } else if (r.type == LogType::DELETE) {
      if (!st->Get(r.key, &probe)) st->Insert(r.key, Tuple::Deserialize(st->schema(), r.image));
    }
  }

  // Checkpoint: the data file now reflects the recovered state; truncate the log.
  Flush();
  wal_.Reset();
}

ExecResult Database::Execute(const std::string &sql) {
  try {
    Statement st = parser_.Parse(sql);
    switch (st.type) {
      case StmtType::CREATE_TABLE: return DoCreate(st.create);
      case StmtType::INSERT:       return DoInsert(st.insert);
      case StmtType::SELECT:       return DoSelect(st.select);
      case StmtType::DELETE:       return DoDelete(st.del);
      case StmtType::BEGIN:
        if (current_txn_) return {false, false, "already in a transaction", {}, {}, ""};
        current_txn_ = txn_mgr_.Begin();
        wal_.Append({0, current_txn_->id(), LogType::BEGIN, "", 0, ""});
        return {true, false, "BEGIN", {}, {}, ""};
      case StmtType::COMMIT:
        if (!current_txn_) return {false, false, "no active transaction", {}, {}, ""};
        wal_.Append({0, current_txn_->id(), LogType::COMMIT, "", 0, ""});
        txn_mgr_.Commit(current_txn_);
        current_txn_ = nullptr;
        return {true, false, "COMMIT", {}, {}, ""};
      case StmtType::ROLLBACK:
        if (!current_txn_) return {false, false, "no active transaction", {}, {}, ""};
        txn_mgr_.Abort(current_txn_);  // replays undo log
        wal_.Append({0, current_txn_->id(), LogType::ABORT, "", 0, ""});
        current_txn_ = nullptr;
        return {true, false, "ROLLBACK", {}, {}, ""};
      default:                     return {false, false, "unsupported statement", {}, {}, ""};
    }
  } catch (const std::exception &e) {
    return {false, false, std::string("ERROR: ") + e.what(), {}, {}, ""};
  }
}

}  // namespace minidb
