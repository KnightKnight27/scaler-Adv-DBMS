#include "transaction_manager.h"
#include <iostream>

CycleDetectedException::CycleDetectedException(TransactionID tid)
    : std::runtime_error("Deadlock detected, aborting tx " +
                         std::to_string(tid)) {}

bool TransactionManager::check_committed(TransactionID tid) {
  std::lock_guard<std::mutex> g(registry_mtx_);
  auto pos = registry_.find(tid);
  if (pos == registry_.end())
    return false;
  return pos->second.state == TransactionState::DONE;
}

bool TransactionManager::check_rolled_back(TransactionID tid) {
  std::lock_guard<std::mutex> g(registry_mtx_);
  auto pos = registry_.find(tid);
  if (pos == registry_.end())
    return false;
  return pos->second.state == TransactionState::ROLLED_BACK;
}

bool TransactionManager::can_see_version(const VersionedRecord &rec,
                                         TransactionID snap,
                                         TransactionID reader) {
  bool creator_visible =
      (rec.created_by == reader) ||
      (check_committed(rec.created_by) && rec.created_by < snap);

  if (!creator_visible)
    return false;

  if (rec.deleted_by == 0)
    return true;

  bool deletion_visible =
      (rec.deleted_by == reader) ||
      (check_committed(rec.deleted_by) && rec.deleted_by < snap);

  return !deletion_visible;
}

bool TransactionManager::detect_cycle(
    TransactionID origin,
    const std::unordered_map<TransactionID, std::unordered_set<TransactionID>>
        &adj) {

  std::unordered_set<TransactionID> seen;
  std::unordered_set<TransactionID> on_stack;

  std::function<bool(TransactionID)> explore = [&](TransactionID cur) -> bool {
    seen.insert(cur);
    on_stack.insert(cur);

    auto neighbors = adj.find(cur);
    if (neighbors != adj.end()) {
      for (TransactionID next : neighbors->second) {
        if (on_stack.count(next))
          return true; // back-edge → cycle
        if (!seen.count(next) && explore(next))
          return true;
      }
    }
    on_stack.erase(cur);
    return false;
  };

  return explore(origin);
}

TransactionID TransactionManager::begin() {
  std::lock_guard<std::mutex> g(registry_mtx_);
  TransactionID tid = id_counter_.fetch_add(1);
  registry_[tid] = TxnContext{tid, tid, TransactionState::RUNNING, false};
  std::cout << "[TX " << tid << "] BEGIN\n";
  return tid;
}

void TransactionManager::commit(TransactionID tid) {
  {
    std::lock_guard<std::mutex> g(registry_mtx_);
    registry_.at(tid).state = TransactionState::DONE;
  }
  free_all_locks(tid);
  std::cout << "[TX " << tid << "] COMMITTED\n";
}

void TransactionManager::abort(TransactionID tid) {
  {
    std::lock_guard<std::mutex> g(storage_mtx_);
    for (auto &[key, chain] : storage_) {
      for (auto &rec : chain) {
        if (rec.created_by == tid)
          rec.deleted_by = tid;
        if (rec.deleted_by == tid)
          rec.deleted_by = 0;
      }
    }
  }
  {
    std::lock_guard<std::mutex> g(registry_mtx_);
    registry_.at(tid).state = TransactionState::ROLLED_BACK;
  }
  free_all_locks(tid);
  std::cout << "[TX " << tid << "] ABORTED\n";
}

std::optional<std::string> TransactionManager::read(TransactionID tid,
                                                    const RecordKey &rk) {
  acquire_lock(rk, tid, LockType::READ_LOCK);

  std::lock_guard<std::mutex> g(storage_mtx_);

  TransactionID snap;
  {
    std::lock_guard<std::mutex> rg(registry_mtx_);
    snap = registry_.at(tid).snap_id;
  }

  auto iter = storage_.find(rk);
  if (iter == storage_.end())
    return std::nullopt;

  for (auto &rec : iter->second) {
    if (can_see_version(rec, snap, tid))
      return rec.data;
  }
  return std::nullopt;
}

void TransactionManager::insert(TransactionID tid, const RecordKey &rk,
                                const std::string &val) {
  acquire_lock(rk, tid, LockType::WRITE_LOCK);
  std::lock_guard<std::mutex> g(storage_mtx_);
  storage_[rk].push_front(VersionedRecord{val, tid, 0});
}

void TransactionManager::update(TransactionID tid, const RecordKey &rk,
                                const std::string &val) {
  acquire_lock(rk, tid, LockType::WRITE_LOCK);
  std::lock_guard<std::mutex> g(storage_mtx_);

  TransactionID snap;
  {
    std::lock_guard<std::mutex> rg(registry_mtx_);
    snap = registry_.at(tid).snap_id;
  }

  auto iter = storage_.find(rk);
  if (iter != storage_.end()) {
    for (auto &rec : iter->second) {
      if (can_see_version(rec, snap, tid) && rec.deleted_by == 0) {
        rec.deleted_by = tid;
        break;
      }
    }
  }
  storage_[rk].push_front(VersionedRecord{val, tid, 0});
}

void TransactionManager::remove(TransactionID tid, const RecordKey &rk) {
  acquire_lock(rk, tid, LockType::WRITE_LOCK);
  std::lock_guard<std::mutex> g(storage_mtx_);

  TransactionID snap;
  {
    std::lock_guard<std::mutex> rg(registry_mtx_);
    snap = registry_.at(tid).snap_id;
  }

  auto iter = storage_.find(rk);
  if (iter == storage_.end())
    return;

  for (auto &rec : iter->second) {
    if (can_see_version(rec, snap, tid) && rec.deleted_by == 0) {
      rec.deleted_by = tid;
      return;
    }
  }
}

void TransactionManager::acquire_lock(const RecordKey &rk, TransactionID tid,
                                      LockType lt) {
  {
    std::lock_guard<std::mutex> g(registry_mtx_);
    if (registry_.at(tid).shrink_phase)
      throw std::runtime_error(
          "2PL violation: cannot acquire lock in shrinking phase");
  }

  KeyLockEntry *entry_ptr = nullptr;
  {
    std::lock_guard<std::mutex> g(lock_mgr_mtx_);
    entry_ptr = &lock_entries_[rk];
  }
  KeyLockEntry &entry = *entry_ptr;
  std::unique_lock<std::mutex> ul(entry.guard);

  for (auto &p : entry.pending_list) {
    if (p.owner == tid && p.is_granted) {
      if (lt == LockType::READ_LOCK)
        return;
      if (p.type == LockType::WRITE_LOCK)
        return;
    }
  }

  entry.pending_list.push_back(PendingLock{tid, lt, false});
  auto &my_entry = entry.pending_list.back();

  for (;;) {
    bool blocked = false;
    std::unordered_set<TransactionID> blockers;

    for (auto &p : entry.pending_list) {
      if (&p == &my_entry)
        break;
      if (!p.is_granted)
        continue;
      if (lt == LockType::WRITE_LOCK || p.type == LockType::WRITE_LOCK) {
        if (p.owner != tid) {
          blocked = true;
          blockers.insert(p.owner);
        }
      }
    }

    if (!blocked) {
      my_entry.is_granted = true;
      {
        std::lock_guard<std::mutex> g(lock_mgr_mtx_);
        dependency_graph_.erase(tid);
      }
      return;
    }

    {
      std::lock_guard<std::mutex> g(lock_mgr_mtx_);
      dependency_graph_[tid] = blockers;
      if (detect_cycle(tid, dependency_graph_)) {
        dependency_graph_.erase(tid);
        entry.pending_list.remove_if([&](const PendingLock &p) {
          return p.owner == tid && !p.is_granted;
        });
        throw CycleDetectedException(tid);
      }
    }

    entry.signal.wait(ul);
  }
}

void TransactionManager::free_all_locks(TransactionID tid) {
  {
    std::lock_guard<std::mutex> g(registry_mtx_);
    if (registry_.count(tid))
      registry_.at(tid).shrink_phase = true;
  }

  std::vector<RecordKey> all_keys;
  {
    std::lock_guard<std::mutex> g(lock_mgr_mtx_);
    all_keys.reserve(lock_entries_.size());
    for (auto &[k, _] : lock_entries_)
      all_keys.push_back(k);
  }

  for (const auto &rk : all_keys) {
    KeyLockEntry *entry_ptr = nullptr;
    {
      std::lock_guard<std::mutex> g(lock_mgr_mtx_);
      entry_ptr = &lock_entries_[rk];
    }
    std::unique_lock<std::mutex> ul(entry_ptr->guard);
    entry_ptr->pending_list.remove_if(
        [&](const PendingLock &p) { return p.owner == tid; });
    entry_ptr->signal.notify_all();
  }

  {
    std::lock_guard<std::mutex> g(lock_mgr_mtx_);
    dependency_graph_.erase(tid);
  }
}