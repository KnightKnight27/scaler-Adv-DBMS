#include "txn/mvcc.h"

namespace minidb {

void MvccStore::Init(int64_t key, int32_t value) {
  Version v;
  v.begin_ts = 0;
  v.end_ts = kInf;
  v.value = value;
  v.creator = INVALID_TXN_ID;
  v.committed = true;
  chains_[key] = {v};
}

bool MvccStore::Read(int64_t key, long snapshot, int32_t* out) const {
  auto it = chains_.find(key);
  if (it == chains_.end()) return false;
  // The visible version is committed and its [begin_ts, end_ts) interval
  // contains the snapshot.
  for (const Version& v : it->second) {
    if (v.committed && v.begin_ts <= snapshot && snapshot < v.end_ts) {
      if (out) *out = v.value;
      return true;
    }
  }
  return false;
}

bool MvccStore::Write(int64_t key, TxnId txn, long snapshot, int32_t value) {
  std::vector<Version>& chain = chains_[key];
  // Write-write conflict if another txn has an open uncommitted version, or if
  // someone committed a newer version after our snapshot (first-committer-wins).
  for (const Version& v : chain) {
    if (!v.committed && v.creator != txn) return false;
    if (v.committed && v.end_ts == kInf && v.begin_ts > snapshot) return false;
  }
  Version nv;
  nv.begin_ts = kInf;   // stamped at commit
  nv.end_ts = kInf;
  nv.value = value;
  nv.creator = txn;
  nv.committed = false;
  chain.push_back(nv);
  return true;
}

void MvccStore::Commit(TxnId txn, long commit_ts) {
  for (auto& kv : chains_) {
    std::vector<Version>& chain = kv.second;
    bool has_pending = false;
    for (const Version& v : chain)
      if (v.creator == txn && !v.committed) { has_pending = true; break; }
    if (!has_pending) continue;

    // Close the previously-current committed version.
    for (Version& v : chain)
      if (v.committed && v.end_ts == kInf && v.begin_ts < commit_ts)
        v.end_ts = commit_ts;
    // Commit this txn's pending versions.
    for (Version& v : chain)
      if (v.creator == txn && !v.committed) {
        v.committed = true;
        v.begin_ts = commit_ts;
      }
  }
}

void MvccStore::Abort(TxnId txn) {
  for (auto& kv : chains_) {
    std::vector<Version>& chain = kv.second;
    for (size_t i = 0; i < chain.size();) {
      if (chain[i].creator == txn && !chain[i].committed)
        chain.erase(chain.begin() + i);
      else
        ++i;
    }
  }
}

size_t MvccStore::VersionCount(int64_t key) const {
  auto it = chains_.find(key);
  return it == chains_.end() ? 0 : it->second.size();
}

}  // namespace minidb
