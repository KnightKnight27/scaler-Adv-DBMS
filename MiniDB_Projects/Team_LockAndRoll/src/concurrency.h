#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <shared_mutex>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include "common.h"

namespace minidb {

enum class CCMode { TWO_PL, MVCC };
enum class TxnState { ACTIVE, COMMITTED, ABORTED };

using RowKey = std::pair<int, int64_t>;

struct Transaction {
  txn_id_t id = INVALID_TXN;
  CCMode mode = CCMode::TWO_PL;
  TxnState state = TxnState::ACTIVE;
  timestamp_t start_ts = 0;
  timestamp_t commit_ts = 0;

  struct Undo {
    bool was_insert;
    int table_oid;
    int64_t key;
    RID rid;
    Tuple tuple;
  };
  std::vector<Undo> undo;

  std::vector<RowKey> mvcc_keys;

  bool autocommit = false;
};

class LockManager {
 public:
  enum class Mode { SHARED, EXCLUSIVE };

  // blocks until granted; throws on deadlock
  void acquire(txn_id_t txn, RowKey key, Mode mode);
  void release_all(txn_id_t txn);
  void reset();

 private:
  struct Entry {
    std::set<txn_id_t> shared;
    txn_id_t exclusive = INVALID_TXN;
  };
  bool compatible(const Entry& e, txn_id_t txn, Mode mode) const;
  bool has_cycle(txn_id_t start);

  std::mutex mtx_;
  std::condition_variable cv_;
  std::map<RowKey, Entry> table_;
  std::map<txn_id_t, std::set<txn_id_t>> waits_for_;
  std::map<txn_id_t, std::set<RowKey>> held_;
};

struct Version {
  timestamp_t begin_ts = 0;
  timestamp_t end_ts = 0;
  txn_id_t creator = INVALID_TXN;
  bool committed = false;
  bool deleted = false;
  Tuple tuple;
  RID rid;
};

class VersionStore {
 public:
  bool read(int table, int64_t key, timestamp_t start_ts, txn_id_t tid, Tuple* out) const;

  void scan(int table, timestamp_t start_ts, txn_id_t tid,
            const std::function<void(int64_t, const Tuple&, RID)>& fn) const;

  // false on a write-write conflict; caller must abort
  bool write(int table, int64_t key, txn_id_t tid, timestamp_t start_ts, bool deleted,
             const Tuple& tuple);

  // taken under the same lock that publishes commits, so a snapshot includes a
  // commit's ts only once its versions are visible
  timestamp_t snapshot() const;

  void commit(Transaction* txn,
              const std::function<RID(int table, int64_t key, bool deleted,
                                      const Tuple&)>& apply);
  void abort(Transaction* txn);

  void put_base(int table, int64_t key, const Tuple& tuple, RID rid);

  void clear();

  size_t version_count() const;

 private:
  // shared so snapshot readers run in parallel; only writers/committers go exclusive
  mutable std::shared_mutex mtx_;
  timestamp_t clock_ = 0;
  std::map<RowKey, std::vector<Version>> chains_;
};

class TransactionManager {
 public:
  explicit TransactionManager(CCMode mode) : mode_(mode) {}

  CCMode mode() const { return mode_; }
  Transaction* begin(bool autocommit = false);
  void end(Transaction* txn);

  size_t aborts() const { return aborts_.load(); }
  void record_abort() { ++aborts_; }

 private:
  CCMode mode_;
  std::mutex mtx_;
  txn_id_t next_id_ = 1;
  std::vector<std::unique_ptr<Transaction>> txns_;
  std::atomic<size_t> aborts_{0};
};

}  // namespace minidb
