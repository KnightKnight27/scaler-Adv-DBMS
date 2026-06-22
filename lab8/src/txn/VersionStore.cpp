#include "txn/VersionStore.h"

#include <algorithm>

namespace txn {

std::optional<Value> VersionStore::readSnapshot(const Key& key, const Transaction& txn) const {
    auto it = heads_.find(key);
    if (it == heads_.end()) return std::nullopt;

    for (const Version* v = it->second.get(); v != nullptr; v = v->older.get()) {
        // A transaction always sees its own pending write (read-your-writes).
        if (v->creator == txn.id) {
            return v->value;
        }
        // Otherwise the newest version committed at or before the snapshot.
        if (v->committed && v->beginTs <= txn.startTs) {
            return v->value;
        }
    }
    return std::nullopt;  // key did not exist as of this snapshot
}

std::optional<Value> VersionStore::readCurrent(const Key& key, const Transaction& txn) const {
    auto it = heads_.find(key);
    if (it == heads_.end()) return std::nullopt;

    for (const Version* v = it->second.get(); v != nullptr; v = v->older.get()) {
        if (v->creator == txn.id) return v->value;  // read-your-writes
        if (v->committed) return v->value;           // newest committed version
    }
    return std::nullopt;
}

void VersionStore::put(const Key& key, Value value, TxnId txn) {
    std::unique_ptr<Version>& head = heads_[key];

    // If the transaction already has a pending version for this key, overwrite
    // it instead of growing the chain with redundant uncommitted nodes.
    if (head && head->creator == txn && !head->committed) {
        head->value = value;
        return;
    }

    auto node = std::make_unique<Version>();
    node->value = value;
    node->creator = txn;
    node->beginTs = kUncommitted;
    node->endTs = kInfinity;
    node->committed = false;
    node->older = std::move(head);
    head = std::move(node);

    writeSet_[txn].push_back(key);
}

void VersionStore::commit(TxnId txn, Timestamp commitTs) {
    auto wsIt = writeSet_.find(txn);
    if (wsIt == writeSet_.end()) return;

    for (const Key& key : wsIt->second) {
        Version* head = heads_[key].get();
        if (head == nullptr || head->creator != txn) continue;

        head->beginTs = commitTs;
        head->committed = true;
        if (head->older) {
            head->older->endTs = commitTs;  // close out the version this one replaced
        }
    }
    writeSet_.erase(wsIt);
}

void VersionStore::abort(TxnId txn) {
    auto wsIt = writeSet_.find(txn);
    if (wsIt == writeSet_.end()) return;

    for (const Key& key : wsIt->second) {
        std::unique_ptr<Version>& head = heads_[key];
        // Discard the transaction's uncommitted head version, exposing the
        // previously committed one again.
        if (head && head->creator == txn && !head->committed) {
            head = std::move(head->older);
        }
    }
    writeSet_.erase(wsIt);
}

void VersionStore::dump(std::ostream& out) const {
    std::vector<Key> keys;
    keys.reserve(heads_.size());
    for (const auto& kv : heads_) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());

    for (const Key& key : keys) {
        out << "  " << key << ": ";
        for (const Version* v = heads_.at(key).get(); v != nullptr; v = v->older.get()) {
            out << "[v=" << v->value << " by T" << v->creator << ' ';
            if (v->committed) {
                out << "begin=" << v->beginTs << " end=";
                if (v->endTs == kInfinity) out << "INF";
                else out << v->endTs;
            } else {
                out << "uncommitted";
            }
            out << ']';
            if (v->older) out << " -> ";
        }
        out << '\n';
    }
}

}  // namespace txn
