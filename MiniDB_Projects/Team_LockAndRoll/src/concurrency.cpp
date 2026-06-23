#include "concurrency.h"

#include <limits>

namespace minidb {

bool LockManager::compatible(const Entry& e, txn_id_t txn, Mode mode) const {
  if (mode == Mode::SHARED) {
    return e.exclusive == INVALID_TXN || e.exclusive == txn;
  }
  if (e.exclusive != INVALID_TXN && e.exclusive != txn) return false;
  for (txn_id_t s : e.shared)
    if (s != txn) return false;
  return true;
}

bool LockManager::has_cycle(txn_id_t start) {
  std::set<txn_id_t> visiting, done;
  std::function<bool(txn_id_t)> dfs = [&](txn_id_t u) -> bool {
    if (visiting.count(u)) return true;
    if (done.count(u)) return false;
    visiting.insert(u);
    auto it = waits_for_.find(u);
    if (it != waits_for_.end())
      for (txn_id_t v : it->second)
        if (dfs(v)) return true;
    visiting.erase(u);
    done.insert(u);
    return false;
  };
  return dfs(start);
}

void LockManager::acquire(txn_id_t txn, RowKey key, Mode mode) {
  std::unique_lock<std::mutex> lk(mtx_);
  Entry& e = table_[key];
  while (!compatible(e, txn, mode)) {
    std::set<txn_id_t> blockers;
    if (e.exclusive != INVALID_TXN && e.exclusive != txn) blockers.insert(e.exclusive);
    for (txn_id_t s : e.shared)
      if (s != txn) blockers.insert(s);
    waits_for_[txn] = blockers;
    if (has_cycle(txn)) {
      waits_for_.erase(txn);
      throw AbortException("deadlock detected; aborting txn " + std::to_string(txn));
    }
    cv_.wait(lk);
    e = table_[key];
  }
  waits_for_.erase(txn);
  if (mode == Mode::EXCLUSIVE) {
    e.shared.erase(txn);
    e.exclusive = txn;
  } else {
    e.shared.insert(txn);
  }
  held_[txn].insert(key);
}

void LockManager::release_all(txn_id_t txn) {
  {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = held_.find(txn);
    if (it != held_.end()) {
      for (const RowKey& key : it->second) {
        Entry& e = table_[key];
        e.shared.erase(txn);
        if (e.exclusive == txn) e.exclusive = INVALID_TXN;
      }
      held_.erase(it);
    }
    waits_for_.erase(txn);
  }
  cv_.notify_all();
}

void LockManager::reset() {
  {
    std::lock_guard<std::mutex> lk(mtx_);
    table_.clear();
    waits_for_.clear();
    held_.clear();
  }
  cv_.notify_all();
}

bool VersionStore::read(int table, int64_t key, timestamp_t start_ts, txn_id_t tid,
                        Tuple* out) const {
  std::shared_lock<std::shared_mutex> lk(mtx_);
  auto it = chains_.find({table, key});
  if (it == chains_.end()) return false;
  const std::vector<Version>& chain = it->second;
  // chain is oldest-first, so newest acceptable version wins
  for (auto rit = chain.rbegin(); rit != chain.rend(); ++rit) {
    const Version& v = *rit;
    bool visible = (!v.committed && v.creator == tid) ||
                   (v.committed && v.begin_ts <= start_ts);
    if (!visible) continue;
    if (v.deleted) return false;
    *out = v.tuple;
    return true;
  }
  return false;
}

void VersionStore::scan(int table, timestamp_t start_ts, txn_id_t tid,
                        const std::function<void(int64_t, const Tuple&, RID)>& fn) const {
  std::shared_lock<std::shared_mutex> lk(mtx_);
  auto lo = chains_.lower_bound({table, std::numeric_limits<int64_t>::min()});
  auto hi = chains_.upper_bound({table, std::numeric_limits<int64_t>::max()});
  for (auto it = lo; it != hi; ++it) {
    const std::vector<Version>& chain = it->second;
    for (auto rit = chain.rbegin(); rit != chain.rend(); ++rit) {
      const Version& v = *rit;
      bool visible = (!v.committed && v.creator == tid) ||
                     (v.committed && v.begin_ts <= start_ts);
      if (!visible) continue;
      if (!v.deleted) fn(it->first.second, v.tuple, v.rid);
      break;
    }
  }
}

bool VersionStore::write(int table, int64_t key, txn_id_t tid, timestamp_t start_ts,
                         bool deleted, const Tuple& tuple) {
  std::unique_lock<std::shared_mutex> lk(mtx_);
  std::vector<Version>& chain = chains_[{table, key}];
  if (!chain.empty()) {
    const Version& top = chain.back();
    if (!top.committed && top.creator != tid) return false;
    // committed after our snapshot: first-committer-wins
    if (top.committed && top.begin_ts > start_ts) return false;
    if (!top.committed && top.creator == tid) {
      chain.back().tuple = tuple;
      chain.back().deleted = deleted;
      return true;
    }
  }
  Version v;
  v.creator = tid;
  v.committed = false;
  v.deleted = deleted;
  v.tuple = tuple;
  chain.push_back(std::move(v));
  return true;
}

timestamp_t VersionStore::snapshot() const {
  std::shared_lock<std::shared_mutex> lk(mtx_);
  return clock_;
}

void VersionStore::commit(Transaction* txn,
                          const std::function<RID(int, int64_t, bool, const Tuple&)>& apply) {
  std::unique_lock<std::shared_mutex> lk(mtx_);
  // commit_ts assigned under the same lock that publishes versions and that
  // snapshot() reads, so a snapshot can't see this ts before the versions
  const timestamp_t commit_ts = ++clock_;
  for (const RowKey& rk : txn->mvcc_keys) {
    auto it = chains_.find(rk);
    if (it == chains_.end()) continue;
    std::vector<Version>& chain = it->second;
    if (chain.empty()) continue;
    Version& v = chain.back();
    if (v.committed || v.creator != txn->id) continue;
    // close the previous live committed version
    for (auto rit = chain.rbegin() + 1; rit != chain.rend(); ++rit) {
      if (rit->committed && rit->end_ts == 0) {
        rit->end_ts = commit_ts;
        break;
      }
    }
    v.begin_ts = commit_ts;
    v.committed = true;
    v.rid = apply(rk.first, rk.second, v.deleted, v.tuple);
  }
}

void VersionStore::abort(Transaction* txn) {
  std::unique_lock<std::shared_mutex> lk(mtx_);
  for (const RowKey& rk : txn->mvcc_keys) {
    auto it = chains_.find(rk);
    if (it == chains_.end()) continue;
    std::vector<Version>& chain = it->second;
    if (!chain.empty() && !chain.back().committed && chain.back().creator == txn->id)
      chain.pop_back();
  }
}

void VersionStore::put_base(int table, int64_t key, const Tuple& tuple, RID rid) {
  std::unique_lock<std::shared_mutex> lk(mtx_);
  Version v;
  v.begin_ts = 0;
  v.end_ts = 0;
  v.committed = true;
  v.deleted = false;
  v.tuple = tuple;
  v.rid = rid;
  chains_[{table, key}].push_back(std::move(v));
}

void VersionStore::clear() {
  std::unique_lock<std::shared_mutex> lk(mtx_);
  chains_.clear();
}

size_t VersionStore::version_count() const {
  std::shared_lock<std::shared_mutex> lk(mtx_);
  size_t n = 0;
  for (auto& [k, c] : chains_) n += c.size();
  return n;
}

Transaction* TransactionManager::begin(bool autocommit) {
  std::lock_guard<std::mutex> lk(mtx_);
  auto t = std::make_unique<Transaction>();
  t->id = next_id_++;
  t->mode = mode_;
  t->state = TxnState::ACTIVE;
  t->start_ts = 0;
  t->autocommit = autocommit;
  Transaction* raw = t.get();
  txns_.push_back(std::move(t));
  return raw;
}

void TransactionManager::end(Transaction* txn) {
  std::lock_guard<std::mutex> lk(mtx_);
  for (auto it = txns_.begin(); it != txns_.end(); ++it) {
    if (it->get() == txn) {
      txns_.erase(it);
      return;
    }
  }
}

}  // namespace minidb
