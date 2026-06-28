#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "minidb/common/types.h"

namespace minidb {

enum class MvccState { Active, Committed, Aborted };

struct MvccTransaction {
  TxnId id{0};
  Timestamp start_ts{0};
  Timestamp commit_ts{0};
  MvccState state{MvccState::Active};
};

struct MvccVersion {
  std::uint64_t version_id{0};
  Rid rid;
  std::string row;
  TxnId writer{0};
  Timestamp begin_ts{0};
  Timestamp end_ts{UINT64_MAX};
  bool committed{false};
  bool deleted{false};
  std::optional<std::uint64_t> supersedes;
};

class MvccStore {
 public:
  MvccTransaction& Begin();
  bool Insert(TxnId txn_id, Rid rid, std::string row);
  bool Update(TxnId txn_id, Rid rid, std::string row);
  bool Delete(TxnId txn_id, Rid rid);
  std::optional<std::string> Read(TxnId txn_id, Rid rid) const;
  std::vector<std::pair<Rid, std::string>> Scan(TxnId txn_id) const;
  void Commit(TxnId txn_id);
  void Abort(TxnId txn_id);
  const MvccTransaction& Get(TxnId txn_id) const;

 private:
  std::string Key(Rid rid) const;
  MvccTransaction& GetMutable(TxnId txn_id);
  bool HasConflictingWriter(TxnId txn_id, Rid rid) const;
  const MvccVersion* LatestCommitted(Rid rid) const;
  const MvccVersion* VisibleVersion(const MvccTransaction& txn, Rid rid) const;
  MvccVersion* FindVersion(std::uint64_t version_id);

  TxnId next_txn_id_{1};
  Timestamp next_ts_{1};
  std::uint64_t next_version_id_{1};
  std::unordered_map<TxnId, MvccTransaction> transactions_;
  std::unordered_map<std::string, std::vector<MvccVersion>> versions_;
};

}  // namespace minidb
