#include "LockManager.h"
#include <algorithm>

bool LockManager::is_compatible(LockMode existing_mode, LockMode requested) const {
    // S+S: compatible. S+X or X+anything: incompatible.
    return existing_mode == LockMode::SHARED && requested == LockMode::SHARED;
}

bool LockManager::already_holds_compatible(TxnId txn_id, const RecordKey& key, LockMode mode) const {
    auto it = lock_table.find(key);
    if (it == lock_table.end()) return false;
    const auto& entry = it->second;
    auto hit = std::find(entry.holders.begin(), entry.holders.end(), txn_id);
    if (hit == entry.holders.end()) return false;
    // Holds X lock -> satisfies any request
    if (entry.mode == LockMode::EXCLUSIVE) return true;
    // Holds S lock -> satisfies S request only
    return mode == LockMode::SHARED;
}

bool LockManager::try_acquire(TxnId txn_id, const RecordKey& key, LockMode mode) {
    if (already_holds_compatible(txn_id, key, mode)) return true;

    auto it = lock_table.find(key);
    if (it == lock_table.end()) {
        // No lock exists: grant immediately
        lock_table[key] = {mode, {txn_id}};
        return true;
    }

    auto& entry = it->second;
    // Lock exists: check compatibility
    if (is_compatible(entry.mode, mode)) {
        entry.holders.push_back(txn_id);
        return true;
    }
    // Incompatible: blocked
    return false;
}

void LockManager::release_all(TxnId txn_id) {
    std::vector<RecordKey> to_remove;
    for (auto& [key, entry] : lock_table) {
        auto it = std::find(entry.holders.begin(), entry.holders.end(), txn_id);
        if (it != entry.holders.end()) {
            entry.holders.erase(it);
            if (entry.holders.empty())
                to_remove.push_back(key);
        }
    }
    for (const auto& k : to_remove)
        lock_table.erase(k);
}

std::vector<TxnId> LockManager::get_holders(const RecordKey& key) const {
    auto it = lock_table.find(key);
    if (it == lock_table.end()) return {};
    return it->second.holders;
}

void LockManager::print_lock_table() const {
    std::cout << "  Lock table:\n";
    for (const auto& [key, entry] : lock_table) {
        std::cout << "    " << key << " ["
                  << (entry.mode == LockMode::EXCLUSIVE ? "X" : "S") << "] holders: ";
        for (TxnId h : entry.holders) std::cout << "T" << h << " ";
        std::cout << "\n";
    }
}
