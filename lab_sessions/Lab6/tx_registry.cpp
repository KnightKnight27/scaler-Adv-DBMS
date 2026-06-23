#include "tx_registry.h"

TxID TransactionRegistry::begin() {
    std::lock_guard<std::mutex> lk(mu_);
    TxID xid = next_xid_++;
    txns_[xid] = Transaction{xid, xid, TxStatus::ACTIVE, false};
    return xid;
}

TxID TransactionRegistry::snapshot_xid(TxID xid) const {
    std::lock_guard<std::mutex> lk(mu_);
    return txns_.at(xid).snapshot_xid;
}

bool TransactionRegistry::is_committed(TxID xid) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = txns_.find(xid);
    return it != txns_.end() && it->second.status == TxStatus::COMMITTED;
}

bool TransactionRegistry::is_aborted(TxID xid) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = txns_.find(xid);
    return it != txns_.end() && it->second.status == TxStatus::ABORTED;
}

bool TransactionRegistry::in_shrinking(TxID xid) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = txns_.find(xid);
    return it != txns_.end() && it->second.in_shrinking;
}

void TransactionRegistry::set_status(TxID xid, TxStatus status) {
    std::lock_guard<std::mutex> lk(mu_);
    txns_.at(xid).status = status;
}

void TransactionRegistry::enter_shrinking(TxID xid) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = txns_.find(xid);
    if (it != txns_.end()) it->second.in_shrinking = true;
}
