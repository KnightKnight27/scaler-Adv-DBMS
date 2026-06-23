#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "minidb/common/types.h"

namespace minidb {

enum class TransactionState { Active, Committed, Aborted };
enum class LockMode { Shared, Exclusive };

struct WriteAction {
  LogType type;
  std::string table;
  Record record;
};

class Transaction {
 public:
  explicit Transaction(TxnId id) : id_(id) {}
  TxnId Id() const { return id_; }
  TransactionState State() const { return state_; }
  std::vector<WriteAction> &WriteSet() { return write_set_; }
  const std::vector<WriteAction> &WriteSet() const { return write_set_; }

 private:
  friend class LockManager;
  TxnId id_;
  TransactionState state_{TransactionState::Active};
  std::unordered_map<std::string, LockMode> locks_;
  std::vector<WriteAction> write_set_;
};

}  // namespace minidb
