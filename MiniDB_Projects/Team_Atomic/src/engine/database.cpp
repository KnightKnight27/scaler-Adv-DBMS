#include "engine/database.h"
#include "sql/parser.h"
#include "record/tuple.h"
#include "optimizer/optimizer.h"
#include <set>

namespace minidb {

Database::Database(const std::string& db_path) : db_path_(db_path) {
  disk_ = std::make_unique<DiskManager>(db_path_ + ".db");
  bpm_ = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, disk_.get());
  catalog_ = std::make_unique<Catalog>(bpm_.get(), db_path_ + ".catalog", db_path_);
  wal_ = std::make_unique<WAL>(db_path_ + ".wal");
  txn_mgr_ = std::make_unique<TransactionManager>();
  Recover();
}

Database::~Database() {
  if (!crashed_) Checkpoint();
}

void Database::Checkpoint() {
  if (catalog_) catalog_->SyncStores();  // flush LSM memtables to SSTables
  if (bpm_) bpm_->FlushAll();
  if (catalog_) catalog_->Save();
  if (wal_) wal_->Truncate();  // log no longer needed once data is durable
}

void Database::SimulateCrash() {
  // Abandon in-memory state with no checkpoint: leak the subsystems so no
  // destructor runs (in particular, no buffer-pool flush). Whatever reached
  // disk stays; dirty pool pages vanish -- exactly a process crash.
  crashed_ = true;
  disk_.release();
  bpm_.release();
  catalog_.release();
  wal_.release();
  txn_mgr_.release();
}

// ---- statement dispatch ----
QueryResult Database::Execute(const std::string& sql) {
  Parser parser(sql);
  Statement s = parser.Parse();
  switch (s.type) {
    case StmtType::CreateTable: return ExecCreate(s);
    case StmtType::Insert:      return ExecInsert(s);
    case StmtType::Select:      return ExecSelect(s);
    case StmtType::Delete:      return ExecDelete(s);
    case StmtType::Begin:       return ExecBegin();
    case StmtType::Commit:      return ExecCommit();
    case StmtType::Abort:       return ExecAbort();
  }
  throw DBError("unsupported statement");
}

QueryResult Database::ExecCreate(const Statement& s) {
  std::vector<Column> cols;
  int pk = -1;
  for (size_t i = 0; i < s.columns.size(); i++) {
    cols.push_back({s.columns[i].name, s.columns[i].type});
    if (s.columns[i].primary_key) {
      if (pk != -1) throw DBError("multiple primary keys");
      pk = static_cast<int>(i);
    }
  }
  catalog_->CreateTable(s.table, Schema(cols, pk), s.engine);
  // DDL acts as a checkpoint so the new table's structure pages are durable
  // (recovery relies on initialized heap/index pages being on disk).
  bpm_->FlushAll();
  QueryResult r;
  r.message = "Table '" + s.table + "' created" +
              (s.engine == EngineType::LSM ? " (LSM engine)" : "");
  return r;
}

txn_id_t Database::CurrentTxn() {
  return in_txn_ ? cur_txn_ : txn_mgr_->Begin();
}

void Database::EndAutocommit(txn_id_t txn) {
  if (in_txn_) return;  // explicit txn: COMMIT handled separately
  wal_->Append({INVALID_LSN, LogType::COMMIT, txn, "", 0, ""});
  wal_->Flush();
  txn_mgr_->Commit(txn);
}

// Idempotent (re)apply of a committed insert: skip if the key already exists.
void Database::ApplyInsert(TableMeta* meta, int64_t key, const std::string& row) {
  std::string tmp;
  if (meta->store->Point(key, &tmp)) return;  // already present
  meta->store->Insert(key, row);
}

// Idempotent delete by key.
void Database::ApplyDelete(TableMeta* meta, int64_t key) {
  std::string tmp;
  if (!meta->store->Point(key, &tmp)) return;  // already gone
  meta->store->Delete(key);
}

QueryResult Database::ExecInsert(const Statement& s) {
  TableMeta* meta = catalog_->GetTable(s.table);
  if (!meta) throw DBError("no such table: " + s.table);
  const Schema& schema = meta->schema;
  if (s.insert_values.size() != schema.ColumnCount())
    throw DBError("INSERT value count does not match column count");

  std::vector<Value> vals;
  for (size_t i = 0; i < schema.ColumnCount(); i++) {
    const Value& v = s.insert_values[i];
    if (v.type != schema.GetColumn(i).type)
      throw DBError("type mismatch for column '" + schema.GetColumn(i).name + "'");
    vals.push_back(v);
  }

  if (!schema.HasPk())
    throw DBError("transactional INSERT requires a primary key on " + s.table);
  int64_t key = vals[schema.PkIndex()].i;

  txn_id_t txn = CurrentTxn();
  txn_mgr_->LockExclusive(txn, s.table + ":" + std::to_string(key));

  std::string existing;
  if (meta->store->Point(key, &existing))
    throw DBError("duplicate primary key: " + std::to_string(key));

  std::string row = Tuple(vals).Serialize(schema);
  // Write-ahead: log the change before applying it.
  wal_->Append({INVALID_LSN, LogType::INSERT, txn, s.table, key, row});
  meta->store->Insert(key, row);
  if (in_txn_) txn_ops_.push_back({LogType::INSERT, s.table, key, row});
  EndAutocommit(txn);

  QueryResult r;
  r.affected = 1;
  r.message = "1 row inserted";
  return r;
}

QueryResult Database::ExecSelect(const Statement& s) {
  Optimizer opt(catalog_.get());
  PlanResult plan = opt.PlanSelect(s);

  QueryResult r;
  r.is_select = true;
  r.plan = plan.description;
  r.schema = plan.root->GetSchema();
  plan.root->Init();
  Row row;
  while (plan.root->Next(&row)) r.rows.push_back(row);
  r.affected = static_cast<int>(r.rows.size());
  return r;
}

QueryResult Database::ExecDelete(const Statement& s) {
  TableMeta* meta = catalog_->GetTable(s.table);
  if (!meta) throw DBError("no such table: " + s.table);
  const Schema& schema = meta->schema;

  OutSchema os;
  for (auto& c : schema.Columns()) os.push_back({s.table, c.name, c.type});
  std::vector<BoundPredicate> preds;
  for (auto& p : s.where) preds.push_back(BindPredicate(os, p));

  if (!schema.HasPk())
    throw DBError("transactional DELETE requires a primary key on " + s.table);

  // Collect matching rows (key + before-image) before mutating anything.
  std::vector<std::pair<int64_t, std::string>> victims;
  auto cursor = meta->store->FullScan();
  std::string bytes;
  while (cursor->Next(&bytes)) {
    Tuple t = Tuple::Deserialize(bytes.data(), schema);
    bool match = true;
    for (auto& p : preds) if (!EvalPredicate(t.Values(), p)) { match = false; break; }
    if (!match) continue;
    victims.emplace_back(t.GetValue(schema.PkIndex()).i, bytes);
  }

  txn_id_t txn = CurrentTxn();
  for (auto& [key, before] : victims) {
    txn_mgr_->LockExclusive(txn, s.table + ":" + std::to_string(key));
    wal_->Append({INVALID_LSN, LogType::DELETE, txn, s.table, key, before});
    meta->store->Delete(key);
    if (in_txn_) txn_ops_.push_back({LogType::DELETE, s.table, key, before});
  }
  EndAutocommit(txn);

  QueryResult r;
  r.affected = static_cast<int>(victims.size());
  r.message = std::to_string(r.affected) + " row(s) deleted";
  return r;
}

QueryResult Database::ExecBegin() {
  if (in_txn_) throw DBError("already in a transaction");
  in_txn_ = true;
  cur_txn_ = txn_mgr_->Begin();
  txn_ops_.clear();
  wal_->Append({INVALID_LSN, LogType::BEGIN, cur_txn_, "", 0, ""});
  QueryResult r;
  r.message = "BEGIN (txn " + std::to_string(cur_txn_) + ")";
  return r;
}

QueryResult Database::ExecCommit() {
  if (!in_txn_) throw DBError("no active transaction");
  wal_->Append({INVALID_LSN, LogType::COMMIT, cur_txn_, "", 0, ""});
  wal_->Flush();                 // write-ahead: durable before we report success
  txn_mgr_->Commit(cur_txn_);
  QueryResult r;
  r.message = "COMMIT (txn " + std::to_string(cur_txn_) + ")";
  in_txn_ = false;
  txn_ops_.clear();
  return r;
}

QueryResult Database::ExecAbort() {
  if (!in_txn_) throw DBError("no active transaction");
  TableMeta* meta = nullptr;
  // Undo applied ops in reverse order.
  for (auto it = txn_ops_.rbegin(); it != txn_ops_.rend(); ++it) {
    meta = catalog_->GetTable(it->table);
    if (!meta) continue;
    if (it->type == LogType::INSERT) ApplyDelete(meta, it->key);
    else /* DELETE */ ApplyInsert(meta, it->key, it->row);
  }
  wal_->Append({INVALID_LSN, LogType::ABORT, cur_txn_, "", 0, ""});
  wal_->Flush();
  txn_mgr_->Abort(cur_txn_);
  QueryResult r;
  r.message = "ROLLBACK (txn " + std::to_string(cur_txn_) + ")";
  in_txn_ = false;
  txn_ops_.clear();
  return r;
}

void Database::Recover() {
  std::vector<LogRecord> recs = wal_->ReadAll();
  if (recs.empty()) return;

  // Pass 1: which transactions committed?
  std::set<txn_id_t> committed;
  for (auto& r : recs)
    if (r.type == LogType::COMMIT) committed.insert(r.txn);

  // Pass 2 (redo): reapply committed inserts/deletes in log order.
  int redo = 0, undo = 0;
  for (auto& r : recs) {
    if (!committed.count(r.txn)) continue;
    TableMeta* meta = catalog_->GetTable(r.table);
    if (!meta) continue;
    if (r.type == LogType::INSERT) { ApplyInsert(meta, r.key, r.row); redo++; }
    else if (r.type == LogType::DELETE) { ApplyDelete(meta, r.key); redo++; }
  }

  // Pass 3 (undo): reverse the effects of uncommitted (loser) transactions.
  for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
    if (committed.count(it->txn)) continue;
    TableMeta* meta = catalog_->GetTable(it->table);
    if (!meta) continue;
    if (it->type == LogType::INSERT) { ApplyDelete(meta, it->key); undo++; }
    else if (it->type == LogType::DELETE) { ApplyInsert(meta, it->key, it->row); undo++; }
  }

  // Recovery complete: make the rebuilt state durable, then clear the log.
  Checkpoint();
  (void)redo; (void)undo;
}

}  // namespace minidb
