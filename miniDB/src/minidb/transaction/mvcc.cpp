#include "minidb/transaction/mvcc.h"

#include <algorithm>

namespace minidb {

MvccTransaction& MvccStore::Begin() {
  TxnId txn_id = next_txn_id_++;
  Timestamp start_ts = next_ts_++;
  auto [it, inserted] = transactions_.emplace(txn_id, MvccTransaction{txn_id, start_ts});
  (void)inserted;
  return it->second;
}

bool MvccStore::Insert(TxnId txn_id, Rid rid, std::string row) {
  MvccTransaction& txn = GetMutable(txn_id);
  if (txn.state != MvccState::Active) {
    throw MiniDbError("MVCC transaction is not active");
  }
  if (HasConflictingWriter(txn_id, rid) || LatestCommitted(rid) != nullptr) {
    return false;
  }

  versions_[Key(rid)].push_back(
      MvccVersion{next_version_id_++, rid, std::move(row), txn_id, txn.start_ts, UINT64_MAX, false, false});
  return true;
}

bool MvccStore::Update(TxnId txn_id, Rid rid, std::string row) {
  MvccTransaction& txn = GetMutable(txn_id);
  if (txn.state != MvccState::Active) {
    throw MiniDbError("MVCC transaction is not active");
  }
  if (HasConflictingWriter(txn_id, rid)) {
    return false;
  }

  const MvccVersion* visible = VisibleVersion(txn, rid);
  if (visible == nullptr || visible->deleted) {
    return false;
  }

  versions_[Key(rid)].push_back(MvccVersion{next_version_id_++, rid, std::move(row), txn_id, txn.start_ts,
                                            UINT64_MAX, false, false, visible->version_id});
  return true;
}

bool MvccStore::Delete(TxnId txn_id, Rid rid) {
  MvccTransaction& txn = GetMutable(txn_id);
  if (txn.state != MvccState::Active) {
    throw MiniDbError("MVCC transaction is not active");
  }
  if (HasConflictingWriter(txn_id, rid)) {
    return false;
  }

  const MvccVersion* visible = VisibleVersion(txn, rid);
  if (visible == nullptr || visible->deleted) {
    return false;
  }

  versions_[Key(rid)].push_back(MvccVersion{next_version_id_++, rid, "", txn_id, txn.start_ts, UINT64_MAX, false,
                                            true, visible->version_id});
  return true;
}

std::optional<std::string> MvccStore::Read(TxnId txn_id, Rid rid) const {
  const auto& txn = Get(txn_id);
  const MvccVersion* version = VisibleVersion(txn, rid);
  if (version == nullptr || version->deleted) {
    return std::nullopt;
  }
  return version->row;
}

std::vector<std::pair<Rid, std::string>> MvccStore::Scan(TxnId txn_id) const {
  const auto& txn = Get(txn_id);
  std::vector<std::pair<Rid, std::string>> rows;
  for (const auto& [key, chain] : versions_) {
    (void)key;
    const MvccVersion* best = nullptr;
    for (const auto& version : chain) {
      if (version.writer == txn.id) {
        best = &version;
      } else if (version.committed && version.begin_ts <= txn.start_ts && txn.start_ts < version.end_ts) {
        if (best == nullptr || version.begin_ts > best->begin_ts) {
          best = &version;
        }
      }
    }
    if (best != nullptr && !best->deleted) {
      rows.push_back({best->rid, best->row});
    }
  }
  std::sort(rows.begin(), rows.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.first.page_id != rhs.first.page_id) {
      return lhs.first.page_id < rhs.first.page_id;
    }
    return lhs.first.slot_id < rhs.first.slot_id;
  });
  return rows;
}

void MvccStore::Commit(TxnId txn_id) {
  MvccTransaction& txn = GetMutable(txn_id);
  if (txn.state != MvccState::Active) {
    throw MiniDbError("MVCC transaction is not active");
  }
  txn.commit_ts = next_ts_++;
  txn.state = MvccState::Committed;

  for (auto& [key, chain] : versions_) {
    (void)key;
    for (auto& version : chain) {
      if (version.writer != txn_id || version.committed) {
        continue;
      }
      version.begin_ts = txn.commit_ts;
      version.committed = true;
      if (version.supersedes.has_value()) {
        auto* old_version = FindVersion(*version.supersedes);
        if (old_version != nullptr) {
          old_version->end_ts = txn.commit_ts;
        }
      }
    }
  }
}

void MvccStore::Abort(TxnId txn_id) {
  MvccTransaction& txn = GetMutable(txn_id);
  if (txn.state != MvccState::Active) {
    throw MiniDbError("MVCC transaction is not active");
  }
  txn.state = MvccState::Aborted;

  for (auto& [key, chain] : versions_) {
    (void)key;
    chain.erase(std::remove_if(chain.begin(), chain.end(), [txn_id](const MvccVersion& version) {
                  return version.writer == txn_id && !version.committed;
                }),
                chain.end());
  }
}

const MvccTransaction& MvccStore::Get(TxnId txn_id) const {
  auto it = transactions_.find(txn_id);
  if (it == transactions_.end()) {
    throw MiniDbError("unknown MVCC transaction: " + std::to_string(txn_id));
  }
  return it->second;
}

std::string MvccStore::Key(Rid rid) const { return rid.ToString(); }

MvccTransaction& MvccStore::GetMutable(TxnId txn_id) {
  auto it = transactions_.find(txn_id);
  if (it == transactions_.end()) {
    throw MiniDbError("unknown MVCC transaction: " + std::to_string(txn_id));
  }
  return it->second;
}

bool MvccStore::HasConflictingWriter(TxnId txn_id, Rid rid) const {
  auto it = versions_.find(Key(rid));
  if (it == versions_.end()) {
    return false;
  }
  for (const auto& version : it->second) {
    if (!version.committed && version.writer != txn_id) {
      return true;
    }
  }
  return false;
}

const MvccVersion* MvccStore::LatestCommitted(Rid rid) const {
  auto it = versions_.find(Key(rid));
  if (it == versions_.end()) {
    return nullptr;
  }
  const MvccVersion* latest = nullptr;
  for (const auto& version : it->second) {
    if (version.committed && !version.deleted && (latest == nullptr || version.begin_ts > latest->begin_ts)) {
      latest = &version;
    }
  }
  return latest;
}

const MvccVersion* MvccStore::VisibleVersion(const MvccTransaction& txn, Rid rid) const {
  auto it = versions_.find(Key(rid));
  if (it == versions_.end()) {
    return nullptr;
  }
  const MvccVersion* best = nullptr;
  for (const auto& version : it->second) {
    if (version.writer == txn.id && !version.committed) {
      best = &version;
    } else if (version.committed && version.begin_ts <= txn.start_ts && txn.start_ts < version.end_ts) {
      if (best == nullptr || version.begin_ts > best->begin_ts) {
        best = &version;
      }
    }
  }
  return best;
}

MvccVersion* MvccStore::FindVersion(std::uint64_t version_id) {
  for (auto& [key, chain] : versions_) {
    (void)key;
    for (auto& version : chain) {
      if (version.version_id == version_id) {
        return &version;
      }
    }
  }
  return nullptr;
}

}  // namespace minidb
