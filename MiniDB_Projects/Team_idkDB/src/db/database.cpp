#include "minidb/db/database.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "minidb/common/trace.h"
#include "minidb/query/executor.h"
#include "minidb/query/parser.h"

namespace minidb {
namespace {

std::string TableFileName(const std::string &table) {
  if (table.empty()) throw std::invalid_argument("table name cannot be empty");
  for (unsigned char c : table) {
    if (!std::isalnum(c) && c != '_') {
      throw std::invalid_argument("table names may contain only letters, digits, and underscore");
    }
  }
  return table + ".tbl";
}

std::string PlanName(AccessPath path) {
  switch (path) {
    case AccessPath::SequentialScan:
      return "sequential scan";
    case AccessPath::IndexScan:
      return "index scan";
    case AccessPath::DirectInsert:
      return "direct insert";
    case AccessPath::NestedLoopJoin:
      return "nested loop join";
    case AccessPath::IndexNestedLoopJoin:
      return "index nested loop join";
  }
  return "unknown";
}

}  // namespace

Database::Table::Table(const std::filesystem::path &path)
    : disk(path), buffer(disk, 16), heap(buffer), index(8) {}

Database::Database(std::filesystem::path directory)
    : directory_(std::move(directory)),
      catalog_path_(directory_ / "catalog.txt"),
      log_(directory_ / "minidb.wal") {
  std::filesystem::create_directories(directory_);
  LoadCatalog();
  Recover();
  for (auto &[name, table] : tables_) {
    (void)name;
    RebuildIndex(*table);
  }
}

Database::~Database() {
  try {
    Flush();
  } catch (...) {
  }
}

void Database::LoadCatalog() {
  std::ifstream in(catalog_path_);
  std::string name;
  while (in >> name) OpenTable(name);
}

void Database::SaveCatalog() const {
  const auto temporary = catalog_path_.string() + ".tmp";
  std::ofstream out(temporary, std::ios::trunc);
  auto names = table_names_;
  std::sort(names.begin(), names.end());
  for (const auto &name : names) out << name << '\n';
  out.flush();
  if (!out) throw std::runtime_error("failed to write catalog");
  out.close();
  std::error_code ec;
  std::filesystem::remove(catalog_path_, ec);
  std::filesystem::rename(temporary, catalog_path_);
}

Database::Table &Database::OpenTable(const std::string &name) {
  const auto found = tables_.find(name);
  if (found != tables_.end()) return *found->second;
  const auto path = directory_ / TableFileName(name);
  auto table = std::make_unique<Table>(path);
  auto *raw = table.get();
  tables_.emplace(name, std::move(table));
  if (std::find(table_names_.begin(), table_names_.end(), name) ==
      table_names_.end()) {
    table_names_.push_back(name);
    SaveCatalog();
  }
  RebuildIndex(*raw);
  return *raw;
}

void Database::RebuildIndex(Table &table) {
  table.index = BPlusTree(8);
  for (const auto &[rid, record] : table.heap.Scan()) {
    table.index.Insert(record.key, rid);
  }
}

void Database::Recover() {
  const auto records = log_.ReadAll();
  std::unordered_set<TxnId> committed;
  std::unordered_set<TxnId> aborted;
  std::unordered_set<TxnId> begun;
  for (const auto &record : records) {
    if (record.type == LogType::Begin) begun.insert(record.txn_id);
    if (record.type == LogType::Commit) committed.insert(record.txn_id);
    if (record.type == LogType::Abort) aborted.insert(record.txn_id);
  }

  // Phase 1: analysis. Winners are committed transactions; losers are begun
  // transactions with no terminal COMMIT/ABORT record.
  std::unordered_set<TxnId> incomplete;
  for (TxnId txn_id : begun) {
    if (!committed.contains(txn_id) && !aborted.contains(txn_id)) {
      incomplete.insert(txn_id);
    }
  }

  // Phase 2: redo committed transactions idempotently.
  for (const auto &record : records) {
    if (!committed.contains(record.txn_id) || aborted.contains(record.txn_id)) {
      continue;
    }
    if (record.type == LogType::Insert) {
      ApplyRedoInsert(record.table, Record{record.key, record.new_value});
    } else if (record.type == LogType::Delete) {
      ApplyRedoDelete(record.table, record.key);
    }
  }

  // Phase 3: undo incomplete transactions in reverse LSN order.
  for (auto it = records.rbegin(); it != records.rend(); ++it) {
    if (!incomplete.contains(it->txn_id)) continue;
    if (it->type == LogType::Insert) {
      UndoInsert(it->table, it->key);
    } else if (it->type == LogType::Delete) {
      UndoDelete(it->table, Record{it->key, it->old_value});
    }
  }
  Flush();
}

void Database::ApplyRedoInsert(const std::string &table_name,
                               const Record &record) {
  auto &table = OpenTable(table_name);
  if (table.index.Search(record.key)) return;
  const RID rid = table.heap.Insert(record);
  table.index.Insert(record.key, rid);
}

void Database::ApplyRedoDelete(const std::string &table_name, int key) {
  auto &table = OpenTable(table_name);
  if (const auto rid = table.index.Search(key)) {
    table.heap.Delete(*rid);
    table.index.Delete(key);
  }
}

void Database::UndoInsert(const std::string &table_name, int key) {
  auto &table = OpenTable(table_name);
  if (const auto rid = table.index.Search(key)) {
    table.heap.Delete(*rid);
    table.index.Delete(key);
  }
}

void Database::UndoDelete(const std::string &table_name,
                          const Record &old_record) {
  if (old_record.value.empty()) return;
  auto &table = OpenTable(table_name);
  if (table.index.Search(old_record.key)) return;
  const RID rid = table.heap.Insert(old_record);
  table.index.Insert(old_record.key, rid);
}

void Database::UndoWriteSet(std::vector<WriteAction> &write_set) {
  for (auto it = write_set.rbegin(); it != write_set.rend(); ++it) {
    if (it->type == LogType::Insert) {
      UndoInsert(it->table, it->record.key);
    } else if (it->type == LogType::Delete) {
      UndoDelete(it->table, it->record);
    }
  }
  write_set.clear();
}

TableStats Database::Stats(const std::string &table_name) {
  auto &table = OpenTable(table_name);
  return {table.index.Size(), table.heap.PageCount(), table.index.Height()};
}

QueryResult Database::Execute(const std::string &sql) {
  const Parser parser;
  const Query query = parser.Parse(sql);
  switch (query.type) {
    case QueryType::Insert:
      return ExecuteInsert(query);
    case QueryType::Select:
      return ExecuteSelect(query);
    case QueryType::Delete:
      return ExecuteDelete(query);
    case QueryType::Join:
      return ExecuteJoin(query);
    case QueryType::Begin:
      return ExecuteBegin(query);
    case QueryType::Commit:
      return ExecuteCommit(query);
    case QueryType::Abort:
      return ExecuteAbort(query);
    case QueryType::Help:
      return {true,
              "Commands: BEGIN | COMMIT | ABORT | INSERT table key value | SELECT table WHERE id=key | SELECT table1 JOIN table2 ON table1.id=table2.id | DELETE table WHERE id=key | EXIT",
              std::nullopt, {}, QueryPlan{query, AccessPath::DirectInsert, 0, 0,
                                          "help command", {}, {}}};
    case QueryType::Exit:
      return {true, "bye", std::nullopt, {},
              QueryPlan{query, AccessPath::DirectInsert, 0, 0,
                        "exit command", {}, {}}};
  }
  throw std::runtime_error("unreachable query type");
}

QueryResult Database::ExecuteInsert(const Query &query) {
  const bool autocommit = current_txn_ == nullptr;
  auto local_txn = autocommit ? locks_.Begin() : nullptr;
  Transaction &txn = autocommit ? *local_txn : *current_txn_;
  auto &write_set = txn.WriteSet();
  if (autocommit) log_.Append(txn.Id(), LogType::Begin);
  try {
    locks_.LockExclusive(txn, query.table);
    auto &table = OpenTable(query.table);
    const auto plan = optimizer_.Optimize(query, table.heap.PageCount(),
                                          table.index.Size(),
                                          table.index.Height());
    if (table.index.Search(query.key)) {
      if (autocommit) {
        log_.Append(txn.Id(), LogType::Abort);
        log_.Flush();
        locks_.Abort(txn);
      }
      return {false, "duplicate primary key " + std::to_string(query.key),
              std::nullopt, {}, plan};
    }
    log_.Append(txn.Id(), LogType::Insert, query.table, query.key, {},
                query.value);
    log_.Flush();
    const RID rid = Executor::Insert(Record{query.key, query.value}, table.heap,
                                     table.index);
    write_set.push_back({LogType::Insert, query.table,
                         Record{query.key, query.value}});
    if (autocommit) {
      log_.Append(txn.Id(), LogType::Commit);
      log_.Flush();
      table.buffer.FlushAll();
      locks_.Commit(txn);
    }
    std::ostringstream out;
    out << "inserted " << query.table << ".id=" << query.key << " at " << rid
        << " using " << PlanName(plan.access_path);
    return {true, out.str(), std::nullopt, {}, plan};
  } catch (...) {
    UndoWriteSet(write_set);
    log_.Append(txn.Id(), LogType::Abort);
    log_.Flush();
    locks_.Abort(txn);
    if (!autocommit) current_txn_.reset();
    throw;
  }
}

QueryResult Database::ExecuteSelect(const Query &query) {
  const bool autocommit = current_txn_ == nullptr;
  auto local_txn = autocommit ? locks_.Begin() : nullptr;
  Transaction &txn = autocommit ? *local_txn : *current_txn_;
  locks_.LockShared(txn, query.table);
  auto &table = OpenTable(query.table);
  const auto plan = optimizer_.Optimize(query, table.heap.PageCount(),
                                        table.index.Size(),
                                        table.index.Height());
  auto record = Executor::Select(plan, table.heap, table.index);
  if (autocommit) locks_.Commit(txn);
  if (!record) {
    return {true, "not found; plan=" + PlanName(plan.access_path),
            std::nullopt, {}, plan};
  }
  return {true, "found id=" + std::to_string(record->key) + " value=" +
                    record->value + "; plan=" + PlanName(plan.access_path),
          record, {*record}, plan};
}

QueryResult Database::ExecuteDelete(const Query &query) {
  const bool autocommit = current_txn_ == nullptr;
  auto local_txn = autocommit ? locks_.Begin() : nullptr;
  Transaction &txn = autocommit ? *local_txn : *current_txn_;
  auto &write_set = txn.WriteSet();
  if (autocommit) log_.Append(txn.Id(), LogType::Begin);
  try {
    locks_.LockExclusive(txn, query.table);
    auto &table = OpenTable(query.table);
    const auto plan = optimizer_.Optimize(query, table.heap.PageCount(),
                                          table.index.Size(),
                                          table.index.Height());
    const auto existing = Executor::Select(plan, table.heap, table.index);
    if (!existing) {
      if (autocommit) {
        log_.Append(txn.Id(), LogType::Abort);
        log_.Flush();
        locks_.Abort(txn);
      }
      return {true, "nothing deleted; key not found", std::nullopt, {}, plan};
    }
    log_.Append(txn.Id(), LogType::Delete, query.table, query.key,
                existing->value, {});
    log_.Flush();
    auto deleted = Executor::Delete(plan, table.heap, table.index);
    if (deleted) write_set.push_back({LogType::Delete, query.table, *deleted});
    if (autocommit) {
      log_.Append(txn.Id(), LogType::Commit);
      log_.Flush();
      table.buffer.FlushAll();
      locks_.Commit(txn);
    }
    return {true, "deleted id=" + std::to_string(query.key), deleted,
            deleted ? std::vector<Record>{*deleted} : std::vector<Record>{},
            plan};
  } catch (...) {
    UndoWriteSet(write_set);
    log_.Append(txn.Id(), LogType::Abort);
    log_.Flush();
    locks_.Abort(txn);
    if (!autocommit) current_txn_.reset();
    throw;
  }
}

QueryResult Database::ExecuteJoin(const Query &query) {
  auto txn = locks_.Begin();
  auto &left = OpenTable(query.table);
  auto &right = OpenTable(query.join_table);

  // Deterministic lock order avoids needless deadlocks for two-table joins.
  if (query.table <= query.join_table) {
    locks_.LockShared(*txn, query.table);
    locks_.LockShared(*txn, query.join_table);
  } else {
    locks_.LockShared(*txn, query.join_table);
    locks_.LockShared(*txn, query.table);
  }

  const auto plan = optimizer_.OptimizeJoin(query, Stats(query.table),
                                            Stats(query.join_table));
  std::vector<Record> joined;

  auto emit_join = [&](const Record &left_record,
                       const Record &right_record) {
    joined.push_back(
        Record{left_record.key, left_record.value + "|" + right_record.value});
  };

  if (query.join_all_on_id) {
    auto &outer = plan.outer_table == query.table ? left : right;
    auto &inner = plan.inner_table == query.table ? left : right;

    if (plan.access_path == AccessPath::IndexNestedLoopJoin) {
      for (const auto &[outer_rid, outer_record] : outer.heap.Scan()) {
        (void)outer_rid;
        const auto inner_rid = inner.index.Search(outer_record.key);
        if (!inner_rid) continue;
        const auto inner_record = inner.heap.Read(*inner_rid);
        if (!inner_record) continue;
        if (plan.outer_table == query.table) {
          emit_join(outer_record, *inner_record);
        } else {
          emit_join(*inner_record, outer_record);
        }
      }
    } else {
      const auto inner_records = inner.heap.Scan();
      for (const auto &[outer_rid, outer_record] : outer.heap.Scan()) {
        (void)outer_rid;
        for (const auto &[inner_rid, inner_record] : inner_records) {
          (void)inner_rid;
          if (outer_record.key != inner_record.key) continue;
          if (plan.outer_table == query.table) {
            emit_join(outer_record, inner_record);
          } else {
            emit_join(inner_record, outer_record);
          }
        }
      }
    }
    locks_.Commit(*txn);
    if (joined.empty()) {
      return {true, "join produced 0 rows; plan=" + PlanName(plan.access_path),
              std::nullopt, {}, plan};
    }
    return {true,
            "join produced " + std::to_string(joined.size()) +
                " row(s); plan=" + PlanName(plan.access_path),
            joined.front(), joined, plan};
  }

  auto left_record =
      Executor::Select(QueryPlan{Query{QueryType::Select, query.table, {},
                                       query.key, {}, false},
                                 AccessPath::IndexScan, 0, 0, {}, {}, {}},
                       left.heap, left.index);
  auto right_record =
      Executor::Select(QueryPlan{Query{QueryType::Select, query.join_table, {},
                                       query.key, {}, false},
                                 AccessPath::IndexScan, 0, 0, {}, {}, {}},
                       right.heap, right.index);
  locks_.Commit(*txn);
  if (!left_record || !right_record) {
    return {true, "join produced 0 rows; plan=" + PlanName(plan.access_path),
            std::nullopt, {}, plan};
  }
  emit_join(*left_record, *right_record);
  return {true,
          "join row: " + query.table + ".id=" +
              std::to_string(left_record->key) + " value=" +
              left_record->value + " | " + query.join_table + ".id=" +
              std::to_string(right_record->key) + " value=" +
              right_record->value + "; plan=" + PlanName(plan.access_path),
          Record{query.key, left_record->value + "|" + right_record->value},
          joined,
          plan};
}

QueryResult Database::ExecuteBegin(const Query &query) {
  if (current_txn_) {
    return {false, "transaction already active", std::nullopt, {},
            QueryPlan{query, AccessPath::DirectInsert, 0, 0,
                      "begin rejected: transaction already active", {}, {}}};
  }
  current_txn_ = locks_.Begin();
  current_txn_->WriteSet().clear();
  log_.Append(current_txn_->Id(), LogType::Begin);
  log_.Flush();
  return {true, "transaction begun T" + std::to_string(current_txn_->Id()),
          std::nullopt, {},
          QueryPlan{query, AccessPath::DirectInsert, 0, 0,
                    "explicit transaction begin", {}, {}}};
}

QueryResult Database::ExecuteCommit(const Query &query) {
  if (!current_txn_) {
    return {false, "no active transaction", std::nullopt, {},
            QueryPlan{query, AccessPath::DirectInsert, 0, 0,
                      "commit rejected: no active transaction", {}, {}}};
  }
  log_.Append(current_txn_->Id(), LogType::Commit);
  log_.Flush();
  Flush();
  locks_.Commit(*current_txn_);
  current_txn_.reset();
  return {true, "transaction committed", std::nullopt, {},
          QueryPlan{query, AccessPath::DirectInsert, 0, 0,
                    "strict 2PL commit releases locks", {}, {}}};
}

QueryResult Database::ExecuteAbort(const Query &query) {
  if (!current_txn_) {
    return {false, "no active transaction", std::nullopt, {},
            QueryPlan{query, AccessPath::DirectInsert, 0, 0,
                      "abort rejected: no active transaction", {}, {}}};
  }
  UndoWriteSet(current_txn_->WriteSet());
  log_.Append(current_txn_->Id(), LogType::Abort);
  log_.Flush();
  Flush();
  locks_.Abort(*current_txn_);
  current_txn_.reset();
  return {true, "transaction aborted and rolled back", std::nullopt, {},
          QueryPlan{query, AccessPath::DirectInsert, 0, 0,
                    "write set undone in reverse order", {}, {}}};
}

std::optional<Record> Database::Get(const std::string &table, int key) {
  Query query{QueryType::Select, table, {}, key, {}};
  auto &opened = OpenTable(table);
  const auto plan = optimizer_.Optimize(query, opened.heap.PageCount(),
                                        opened.index.Size(),
                                        opened.index.Height());
  return Executor::Select(plan, opened.heap, opened.index);
}

std::size_t Database::RowCount(const std::string &table) {
  return OpenTable(table).index.Size();
}

void Database::Flush() {
  log_.Flush();
  for (auto &[name, table] : tables_) {
    (void)name;
    table->buffer.FlushAll();
  }
  SaveCatalog();
}

}  // namespace minidb
