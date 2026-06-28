#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "minidb/common/types.h"

namespace minidb {

enum class WalRecordType { Begin, Insert, Delete, Commit, Abort };

struct WalRecord {
  std::uint64_t lsn{0};
  WalRecordType type{WalRecordType::Begin};
  TxnId txn_id{0};
  std::string table;
  Rid rid;
  std::string row;
};

struct RecoveryResult {
  std::unordered_map<std::string, std::vector<std::pair<Rid, std::string>>> rows;
  std::unordered_set<TxnId> committed_txns;
  std::unordered_set<TxnId> ignored_txns;
};

class WalLogManager {
 public:
  explicit WalLogManager(std::filesystem::path path);

  void AppendBegin(TxnId txn_id);
  void AppendInsert(TxnId txn_id, std::string table, Rid rid, std::string row);
  void AppendDelete(TxnId txn_id, std::string table, Rid rid, std::string old_row);
  void AppendCommit(TxnId txn_id);
  void AppendAbort(TxnId txn_id);
  void Flush();

  std::vector<WalRecord> ReadAll() const;
  RecoveryResult Recover() const;

 private:
  void Append(WalRecord record);

  std::filesystem::path path_;
  std::uint64_t next_lsn_{1};
};

}  // namespace minidb
