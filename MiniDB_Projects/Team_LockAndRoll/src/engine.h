#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "catalog.h"
#include "concurrency.h"
#include "parser.h"
#include "storage.h"
#include "wal.h"

namespace minidb {

struct ResultSet {
  bool is_query = false;
  std::vector<std::string> columns;
  std::vector<std::vector<Value>> rows;
  std::string message;
  size_t affected = 0;
};

class Database {
 public:
  Database(const std::string& dir, CCMode mode = CCMode::TWO_PL, size_t pool_frames = 256);
  ~Database();

  CCMode mode() const { return mode_; }
  Catalog& catalog() { return catalog_; }
  BufferPool& buffer_pool() { return bpool_; }
  TransactionManager& txn_manager() { return txnmgr_; }
  VersionStore& version_store() { return vstore_; }

  ResultSet execute(const std::string& sql);

  // if a row-API call throws, the txn must be ended with abort() — not reused or committed
  Transaction* begin(bool autocommit = false) {
    Transaction* t = txnmgr_.begin(autocommit);
    if (mode_ == CCMode::MVCC) t->start_ts = vstore_.snapshot();  // consistent snapshot under the version store's lock
    return t;
  }
  void commit(Transaction* txn);
  void abort(Transaction* txn);

  bool read_key(Transaction* txn, TableInfo* t, int64_t key, Tuple* out);
  void scan_table(Transaction* txn, TableInfo* t,
                  const std::function<bool(RID, const Tuple&)>& fn);
  void insert_row(Transaction* txn, TableInfo* t, const Tuple& tuple);
  size_t delete_row(Transaction* txn, TableInfo* t, int64_t key);

  void checkpoint();
  void simulate_crash_and_recover();
  // off isolates concurrency-control cost from disk latency in the benchmark
  void set_durable(bool on) { log_.set_sync_on_commit(on); }

 private:
  void rebuild_memory_state();
  TableInfo* table_by_oid(int oid) const;
  RID apply_committed(txn_id_t txn, int oid, int64_t key, bool deleted, const Tuple& tuple);

  ResultSet exec_create(const CreateTableStmt& s);
  ResultSet exec_insert(const InsertStmt& s, Transaction* txn);
  ResultSet exec_select(const SelectStmt& s, Transaction* txn);
  ResultSet exec_delete(const DeleteStmt& s, Transaction* txn);

  std::string dir_;
  CCMode mode_;
  DiskManager dm_;
  BufferPool bpool_;
  LogManager log_;
  Catalog catalog_;
  TransactionManager txnmgr_;
  LockManager lockmgr_;
  VersionStore vstore_;
  Transaction* current_txn_ = nullptr;  // active explicit (BEGIN) transaction
};

}  // namespace minidb
