#include "transaction.h"

namespace minidb {

Transaction* TransactionManager::Begin() {
    std::lock_guard<std::mutex> g(mtx_);
    int id = next_id_.fetch_add(1);
    auto txn = std::make_unique<Transaction>(id, Now());
    Transaction* raw = txn.get();
    txns_[id] = std::move(txn);
    return raw;
}

Transaction* TransactionManager::Get(int id) {
    std::lock_guard<std::mutex> g(mtx_);
    auto it = txns_.find(id);
    return it == txns_.end() ? nullptr : it->second.get();
}

void TransactionManager::Commit(Transaction* t) {
    t->SetCommitTs(Now());
    t->SetState(TxnState::COMMITTED);
    // MVCC version stamping happens in the version store at write time using commit_ts;
    // here we only need to release the 2PL locks (Strict 2PL: all at commit).
    lm_->ReleaseAll(t->id());
}

void TransactionManager::Abort(Transaction* t) {
    t->SetState(TxnState::ABORTED);
    // Data rollback (discarding the versions recorded in t->undo()) is performed by the
    // VersionStore in the MVCC build; locks are always released here.
    lm_->ReleaseAll(t->id());
}

}  // namespace minidb
