#pragma once

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using TransactionID = std::uint64_t;
using RecordKey = std::string;

enum class TransactionState { RUNNING, DONE, ROLLED_BACK };
enum class LockType { READ_LOCK, WRITE_LOCK };

struct TxnContext {
  TransactionID txn_id = 0;
  TransactionID snap_id = 0;
  TransactionState state = TransactionState::RUNNING;
  bool shrink_phase = false;
};

struct VersionedRecord {
  std::string data;
  TransactionID created_by = 0;
  TransactionID deleted_by = 0;
};

struct PendingLock {
  TransactionID owner;
  LockType type;
  bool is_granted = false;
};

struct KeyLockEntry {
  std::list<PendingLock> pending_list;
  std::mutex guard;
  std::condition_variable signal;
};

class CycleDetectedException : public std::runtime_error {
public:
  explicit CycleDetectedException(TransactionID tid);
};

class TransactionManager {
public:
  // Transaction lifecycle
  TransactionID begin();
  void commit(TransactionID tid);
  void abort(TransactionID tid);

  // Data manipulation
  std::optional<std::string> read(TransactionID tid, const RecordKey &rk);
  void insert(TransactionID tid, const RecordKey &rk, const std::string &val);
  void update(TransactionID tid, const RecordKey &rk, const std::string &val);
  void remove(TransactionID tid, const RecordKey &rk);

  void acquire_lock(const RecordKey &rk, TransactionID tid, LockType lt);

private:
  std::atomic<TransactionID> id_counter_{1};
  std::mutex registry_mtx_;
  std::unordered_map<TransactionID, TxnContext> registry_;

  std::mutex storage_mtx_;
  std::unordered_map<RecordKey, std::list<VersionedRecord>> storage_;

  std::mutex lock_mgr_mtx_;
  std::unordered_map<RecordKey, KeyLockEntry> lock_entries_;
  std::unordered_map<TransactionID, std::unordered_set<TransactionID>>
      dependency_graph_;

  bool check_committed(TransactionID tid);
  bool check_rolled_back(TransactionID tid);
  bool can_see_version(const VersionedRecord &rec, TransactionID snap,
                       TransactionID reader);
  bool detect_cycle(
      TransactionID origin,
      const std::unordered_map<TransactionID, std::unordered_set<TransactionID>>
          &adj);
  void free_all_locks(TransactionID tid);
};