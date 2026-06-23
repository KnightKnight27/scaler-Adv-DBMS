#pragma once

#include "recovery/log_manager.h"
#include "transaction/lock_manager.h"
#include "transaction/txn.h"

#include <memory>

namespace minidb {

using namespace std;

enum class TxnState { GROWING, SHRINKING, COMMITTED, ABORTED };

class Transaction {
public:
  Transaction(TxnId id, LogManager* lm, LockManager* lck) : id_(id), lm_(lm), lck_(lck) {
    state_ = TxnState::GROWING;
    LogRecord r;
    r.type = LogRecordType::BEGIN;
    r.txnId = id_;
    lm_->Append(r);
  }

  ~Transaction() {
    if (state_ == TxnState::GROWING)
      Abort();
    if (lck_)
      lck_->UnlockAll(id_);
  }

  TxnId GetId() const {
    return id_;
  }
  TxnState GetState() const {
    return state_;
  }

  void Commit() {
    if (state_ == TxnState::COMMITTED || state_ == TxnState::ABORTED)
      return;
    state_ = TxnState::SHRINKING;
    LogRecord r;
    r.type = LogRecordType::COMMIT;
    r.txnId = id_;
    lm_->Append(r);
    state_ = TxnState::COMMITTED;
  }

  void Abort() {
    if (state_ == TxnState::COMMITTED || state_ == TxnState::ABORTED)
      return;
    state_ = TxnState::SHRINKING;
    LogRecord r;
    r.type = LogRecordType::ABORT;
    r.txnId = id_;
    lm_->Append(r);
    state_ = TxnState::ABORTED;
  }

  bool LockShared(int32_t rid) {
    if (state_ == TxnState::COMMITTED || state_ == TxnState::ABORTED)
      return false;
    return lck_->LockShared(id_, rid);
  }

  bool LockExclusive(int32_t rid) {
    if (state_ == TxnState::COMMITTED || state_ == TxnState::ABORTED ||
        state_ == TxnState::SHRINKING)
      return false;
    return lck_->LockExclusive(id_, rid);
  }

  void LogInsert(int32_t pageId, int32_t slotId, const string& newData) {
    if (state_ == TxnState::COMMITTED || state_ == TxnState::ABORTED)
      return;
    if (newData.size() > 4096)
      return;
    LogRecord r;
    r.type = LogRecordType::INSERT;
    r.txnId = id_;
    r.pageId = pageId;
    r.slotId = slotId;
    r.newData = newData;
    lm_->Append(r);
  }

private:
  TxnId id_;
  LogManager* lm_;
  LockManager* lck_;
  TxnState state_;
};

} // namespace minidb