#include "concurrency/lock_manager.h"

#include "common/exception.h"

namespace minidb {

void LockManager::LockShared(Transaction *txn, const RID &rid) {
  Acquire(txn, rid, LockMode::kShared);
}

void LockManager::LockExclusive(Transaction *txn, const RID &rid) {
  Acquire(txn, rid, LockMode::kExclusive);
}

void LockManager::Acquire(Transaction *txn, const RID &rid, LockMode mode) {
  std::unique_lock<std::mutex> lk(latch_);
  LockEntry &entry = locks_[rid];
  txn_id_t me = txn->GetId();

  while (true) {
    // Inspect conflicting holders (everyone but me). An exclusive request
    // conflicts with any holder; a shared request conflicts only with X holders.
    bool conflict = false;
    bool i_hold_shared = false;
    bool oldest_conflict_is_older = false;
    for (const Holder &h : entry.holders) {
      if (h.txn_id == me) {
        if (h.mode == LockMode::kExclusive || mode == LockMode::kShared) {
          return;  // already hold a sufficient lock (re-entrant / no upgrade needed)
        }
        i_hold_shared = true;
        continue;
      }
      bool conflicts = (mode == LockMode::kExclusive) || (h.mode == LockMode::kExclusive);
      if (conflicts) {
        conflict = true;
        if (h.txn_id < me) oldest_conflict_is_older = true;  // an older txn blocks me
      }
    }

    if (!conflict) {
      if (i_hold_shared && mode == LockMode::kExclusive) {
        // Upgrade in place: I am the only holder, so promote my shared lock.
        for (Holder &h : entry.holders) {
          if (h.txn_id == me) h.mode = LockMode::kExclusive;
        }
      } else if (!i_hold_shared) {
        entry.holders.push_back({me, mode});
      }
      txn->LockSet().insert(rid);
      return;
    }

    // Conflict: wait-die. If any older transaction holds a conflicting lock, the
    // younger requester (me) must die; otherwise wait for the younger holders.
    if (oldest_conflict_is_older) {
      txn->SetState(TxnState::kAborted);
      throw Exception(ErrorKind::kAbort,
                      "wait-die: transaction " + std::to_string(me) + " aborted to prevent deadlock");
    }
    entry.cv.wait(lk);
  }
}

void LockManager::UnlockAll(Transaction *txn) {
  std::unique_lock<std::mutex> lk(latch_);
  txn_id_t me = txn->GetId();
  for (const RID &rid : txn->LockSet()) {
    auto it = locks_.find(rid);
    if (it == locks_.end()) continue;
    it->second.holders.remove_if([me](const Holder &h) { return h.txn_id == me; });
    it->second.cv.notify_all();
  }
  txn->LockSet().clear();
}

}  // namespace minidb
