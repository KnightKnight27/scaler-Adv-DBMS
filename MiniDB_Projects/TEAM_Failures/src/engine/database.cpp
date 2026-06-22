#include "engine/database.h"

#include "execution/executor.h"
#include "recovery/recovery_manager.h"
#include "sql/parser.h"

namespace minidb {

// =============================== lifecycle =================================
Database::Database(const string &path) {
  disk_     = make_unique<DiskManager>(path + ".db");
  bpm_      = make_unique<BufferPool>(disk_.get());
  log_      = make_unique<LogManager>(path + ".wal");
  bpm_->setLogManager(log_.get());            // enforce WAL-before-page
  catalog_  = make_unique<Catalog>(bpm_.get(), path + ".catalog");
  lock_mgr_ = make_unique<LockManager>();

  catalog_->load();                            // table schemas + heap locations
  RecoveryManager(log_.get(), catalog_.get(), bpm_.get()).recover();  // redo/undo
  catalog_->rebuildAllIndexes();               // (re)build indexes + row counts
}

Database::~Database() {
  bpm_->flushAll();   // clean shutdown: persist everything
  log_->flush();
  disk_->shutdown();
}

// ============================ transaction control ==========================
// Build a control-only WAL record (BEGIN/COMMIT/ABORT carry no data fields).
static LogRecord controlRecord(txn_id_t txn, LogType type) {
  LogRecord r;
  r.txn = txn;
  r.type = type;
  return r;
}

Transaction *Database::begin() {
  auto *txn = new Transaction(next_txn_id_++);
  log_->append(controlRecord(txn->id(), LogType::kBegin));
  return txn;
}

void Database::commit(Transaction *txn) {
  log_->append(controlRecord(txn->id(), LogType::kCommit));
  log_->flush();                  // durability: a committed txn survives a crash
  lock_mgr_->releaseAll(txn);     // Strict 2PL: release all locks at the end
  txn->set_state(TxnState::kCommitted);
  delete txn;
}

void Database::abort(Transaction *txn) {
  // Undo this txn's changes in reverse order (its in-memory write set).
  auto &ws = txn->write_set();
  for (auto it = ws.rbegin(); it != ws.rend(); ++it) {
    TableInfo *t = catalog_->getTable(it->table);
    if (t == nullptr) continue;
    Tuple tup = Tuple::deserialize(it->tuple_bytes.data(), t->schema);
    if (it->kind == WriteRecord::kInsert) {       // undo insert -> delete it
      t->heap->markDelete(it->rid);
      for (auto &idx : t->indexes) idx->tree->remove(tup.value(idx->col_idx), it->rid);
      t->num_tuples--;
    } else {                                       // undo delete -> restore it
      t->heap->recoverInsert(it->rid, it->tuple_bytes);
      for (auto &idx : t->indexes) idx->tree->insert(tup.value(idx->col_idx), it->rid);
      t->num_tuples++;
    }
  }
  log_->append(controlRecord(txn->id(), LogType::kAbort));
  log_->flush();
  lock_mgr_->releaseAll(txn);
  txn->set_state(TxnState::kAborted);
  delete txn;
}

Database::Result Database::executeAutoCommit(const string &sql) {
  unique_ptr<Statement> stmt = Parser::parse(sql);

  // DDL does not run inside a user transaction (it commits immediately).
  if (stmt->type == StmtType::kCreateTable || stmt->type == StmtType::kCreateIndex)
    return execute(stmt.get(), nullptr);

  // Everything else gets its own transaction: commit on success, abort on error.
  Transaction *txn = begin();
  try {
    Result r = execute(stmt.get(), txn);
    commit(txn);
    return r;
  } catch (...) {
    abort(txn);
    throw;
  }
}

Database::Result Database::execute(Statement *stmt, Transaction *txn) {
  switch (stmt->type) {
    case StmtType::kCreateTable: return doCreateTable(static_cast<CreateTableStmt *>(stmt));
    case StmtType::kCreateIndex: return doCreateIndex(static_cast<CreateIndexStmt *>(stmt));
    case StmtType::kInsert:      return doInsert(static_cast<InsertStmt *>(stmt), txn);
    case StmtType::kDelete:      return doDelete(static_cast<DeleteStmt *>(stmt), txn);
    case StmtType::kSelect:      return doSelect(static_cast<SelectStmt *>(stmt), txn);
    default: throw ExecError("transaction control must be handled by the session");
  }
}

// ================================== DDL ====================================
Database::Result Database::doCreateTable(CreateTableStmt *s) {
  vector<Column> cols;
  int pk = -1;
  for (size_t i = 0; i < s->columns.size(); ++i) {
    cols.push_back({s->columns[i].name, s->columns[i].type});
    if (s->columns[i].primary_key) pk = static_cast<int>(i);
  }
  catalog_->createTable(s->table, Schema(move(cols)), pk);
  Result r;
  r.message = "table '" + s->table + "' created";
  return r;
}

Database::Result Database::doCreateIndex(CreateIndexStmt *s) {
  catalog_->createIndex(s->index_name, s->table, s->column);
  Result r;
  r.message = "index '" + s->index_name + "' created on " + s->table + "(" + s->column + ")";
  return r;
}

// ================================ INSERT ===================================
Database::Result Database::doInsert(InsertStmt *s, Transaction *txn) {
  TableInfo *t = catalog_->getTable(s->table);
  if (t == nullptr) throw BinderError("no such table: " + s->table);

  lock_mgr_->lockExclusive(txn, t->name);   // writers take an exclusive table lock

  if (s->values.size() != t->schema.size())
    throw ExecError("INSERT has " + to_string(s->values.size()) +
                    " values but table has " + to_string(t->schema.size()) + " columns");
  for (size_t i = 0; i < s->values.size(); ++i)
    if (s->values[i].type() != t->schema.column(i).type)
      throw ExecError("type mismatch for column '" + t->schema.column(i).name + "'");

  Tuple tup(s->values);

  // Enforce primary-key uniqueness via the PK index.
  if (t->pk_col >= 0) {
    RID dummy;
    if (t->indexes[0]->tree->search(s->values[t->pk_col], &dummy))
      throw ExecError("duplicate primary key: " + s->values[t->pk_col].toString());
  }

  string bytes = tup.serialize(t->schema);
  RID rid = t->heap->insertTuple(bytes);                 // write to a heap page (in buffer)
  log_->append({INVALID_LSN, txn->id(), LogType::kInsert, t->name, rid, bytes});  // WAL
  for (auto &idx : t->indexes)                           // keep indexes in sync
    idx->tree->insert(s->values[idx->col_idx], rid);
  t->num_tuples++;
  txn->addWrite({WriteRecord::kInsert, t->name, rid, bytes});

  Result r;
  r.affected = 1;
  r.message = "1 row inserted";
  return r;
}

// ================================ DELETE ===================================
Database::Result Database::doDelete(DeleteStmt *s, Transaction *txn) {
  TableInfo *t = catalog_->getTable(s->table);
  if (t == nullptr) throw BinderError("no such table: " + s->table);

  lock_mgr_->lockExclusive(txn, t->name);

  vector<Predicate> preds = predicatesForTable(t, s->where);
  vector<ColumnMeta> cols = makeTableColumns(t);

  // Use the optimizer to find matching rows (index range or full scan).
  ScanChoice c = Optimizer::chooseScan(t, preds);
  vector<pair<RID, string>> matches;
  if (c.use_index) {
    for (RID rid : c.index->tree->range(c.low.get(), c.high.get())) {
      string bytes;
      if (!t->heap->getTuple(rid, &bytes)) continue;
      Tuple tup = Tuple::deserialize(bytes.data(), t->schema);
      if (evalAll(preds, cols, tup)) matches.emplace_back(rid, bytes);
    }
  } else {
    for (auto it = t->heap->begin(); it != t->heap->end(); it.advance()) {
      Tuple tup = Tuple::deserialize(it.bytes().data(), t->schema);
      if (evalAll(preds, cols, tup)) matches.emplace_back(it.rid(), it.bytes());
    }
  }

  for (auto &[rid, bytes] : matches) {
    Tuple tup = Tuple::deserialize(bytes.data(), t->schema);
    log_->append({INVALID_LSN, txn->id(), LogType::kDelete, t->name, rid, bytes});  // WAL
    t->heap->markDelete(rid);
    for (auto &idx : t->indexes) idx->tree->remove(tup.value(idx->col_idx), rid);
    t->num_tuples--;
    txn->addWrite({WriteRecord::kDelete, t->name, rid, bytes});
  }

  Result r;
  r.affected = static_cast<int>(matches.size());
  r.message = to_string(r.affected) + " row(s) deleted";
  return r;
}

// ================================ SELECT ===================================
unique_ptr<Executor> Database::buildScan(TableInfo *t, const ScanChoice &c) {
  if (c.use_index) {
    auto low  = c.low  ? make_unique<Value>(*c.low)  : nullptr;
    auto high = c.high ? make_unique<Value>(*c.high) : nullptr;
    return make_unique<IndexScanExecutor>(t, c.index, move(low),
                                               move(high), c.residual);
  }
  return make_unique<SeqScanExecutor>(t, c.residual);
}

Database::Result Database::doSelect(SelectStmt *s, Transaction *txn) {
  TableInfo *t = catalog_->getTable(s->table);
  if (t == nullptr) throw BinderError("no such table: " + s->table);
  if (txn) lock_mgr_->lockShared(txn, t->name);   // readers take a shared lock

  unique_ptr<Executor> exec;
  string plan;

  if (!s->has_join) {
    vector<Predicate> preds = predicatesForTable(t, s->where);
    ScanChoice c = Optimizer::chooseScan(t, preds);
    plan = c.explain;
    exec = buildScan(t, c);
  } else {
    TableInfo *t2 = catalog_->getTable(s->join_table);
    if (t2 == nullptr) throw BinderError("no such table: " + s->join_table);
    if (txn) lock_mgr_->lockShared(txn, t2->name);

    vector<Predicate> preds1 = predicatesForTable(t, s->where);
    vector<Predicate> preds2 = predicatesForTable(t2, s->where);

    // Figure out each table's join column from "ON a = b".
    auto belongs = [](const string &tbl, const string &col, TableInfo *cand) {
      return tbl.empty() ? cand->schema.getColIdx(col) >= 0 : tbl == cand->name;
    };
    string col1, col2;  // join columns of t and t2 respectively
    for (auto ref : {make_pair(s->join_left_table, s->join_left_col),
                     make_pair(s->join_right_table, s->join_right_col)}) {
      if (belongs(ref.first, ref.second, t)) col1 = ref.second;
      else if (belongs(ref.first, ref.second, t2)) col2 = ref.second;
    }
    if (col1.empty() || col2.empty()) throw BinderError("invalid JOIN condition");

    // Optimizer picks the outer (driving) table; the inner is probed per row.
    bool t_outer = Optimizer::aShouldBeOuter(t, preds1, t2, preds2);
    TableInfo *outer = t_outer ? t : t2;
    TableInfo *inner = t_outer ? t2 : t;
    vector<Predicate> outer_preds = t_outer ? preds1 : preds2;
    vector<Predicate> inner_preds = t_outer ? preds2 : preds1;
    string outer_col = t_outer ? col1 : col2;
    string inner_col = t_outer ? col2 : col1;

    ScanChoice oc = Optimizer::chooseScan(outer, outer_preds);
    auto outer_exec = buildScan(outer, oc);

    int inner_join_idx = inner->schema.getColIdx(inner_col);
    IndexInfo *inner_index = inner->indexOnColumn(inner_join_idx);

    plan = "NestedLoopJoin [outer: " + oc.explain + "] x [inner: " +
           (inner_index ? "IndexScan on " + inner->name + "." + inner_col
                        : "SeqScan on " + inner->name) + "]";

    exec = make_unique<NestedLoopJoinExecutor>(
        move(outer_exec), inner, inner_index, outer->name, outer_col,
        inner_col, inner_preds);
  }

  // Projection: SELECT * keeps everything; otherwise pick the named columns.
  vector<ColumnMeta> out_cols;
  if (!s->star) {
    vector<int> idxs;
    vector<ColumnMeta> proj_cols;
    for (auto &ref : s->columns) {
      int i = resolveColumn(exec->columns(), ref.first, ref.second);
      if (i < 0) throw BinderError("unknown column: " + ref.second);
      idxs.push_back(i);
      proj_cols.push_back(exec->columns()[i]);
    }
    out_cols = proj_cols;
    exec = make_unique<ProjectionExecutor>(move(exec), idxs, proj_cols);
  } else {
    out_cols = exec->columns();
  }

  // Pull all rows up through the operator tree.
  Result r;
  r.plan = plan;
  bool qualify = s->has_join;   // show "table.col" headers only for joins
  for (auto &cm : out_cols)
    r.columns.push_back(qualify ? cm.table + "." + cm.name : cm.name);

  exec->init();
  Tuple row;
  while (exec->next(&row)) r.rows.push_back(row);
  r.affected = static_cast<int>(r.rows.size());
  return r;
}

// =============================== helpers ===================================
vector<Predicate> Database::predicatesForTable(TableInfo *t,
                                                    const vector<Predicate> &all) {
  vector<Predicate> out;
  for (auto &p : all) {
    bool match = p.table.empty() ? (t->schema.getColIdx(p.column) >= 0)
                                  : (p.table == t->name);
    if (match) out.push_back(p);
  }
  return out;
}

}  // namespace minidb
