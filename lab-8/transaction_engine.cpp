#include "transaction_engine.h"
#include <algorithm>

namespace mvcc_ledger {

std::string formatStatus(ExecStatus status) {
    switch (status) {
        case ExecStatus::Success:        return "SUCCESS";
        case ExecStatus::KeyMissing:     return "KEY_MISSING";
        case ExecStatus::WaitBlocked:    return "WAIT_BLOCKED";
        case ExecStatus::ForceAborted:   return "FORCE_ABORTED";
        case ExecStatus::CommitConflict: return "COMMIT_CONFLICT";
    }
    return "UNKNOWN_STATUS";
}

TxID LedgerCoordinator::beginTransaction() {
    TxID newSessionId = tx_sequence_tracker_++;
    
    LedgerSession session;
    session.session_id = newSessionId;
    session.view_timestamp = global_clock_;
    session.state = Lifecycle::InProgress;
    
    active_sessions_.emplace(newSessionId, std::move(session));
    return newSessionId;
}

const LedgerCoordinator::AssetVersion* LedgerCoordinator::fetchSnapshot(const std::string& key, TxID timestamp) const {
    auto historyIterator = historical_ledger_.find(key);
    if (historyIterator == historical_ledger_.end()) {
        return nullptr;
    }

    for (const AssetVersion& version : historyIterator->second) {
        bool mintedBeforeSnapshot = (version.mint_timestamp <= timestamp);
        bool notBurnedOrBurnedLater = (version.burn_timestamp == 0) || (version.burn_timestamp > timestamp);
        
        if (mintedBeforeSnapshot && notBurnedOrBurnedLater) {
            return &version;
        }
    }
    return nullptr;
}

ExecStatus LedgerCoordinator::readRecord(TxID tid, const std::string& accountKey, std::string& outBalance) {
    LedgerSession& session = active_sessions_.at(tid);
    if (session.state != Lifecycle::InProgress) return ExecStatus::ForceAborted;

    // 1. Check local workspace first (uncommitted staged mutations)
    auto localMatch = session.local_mutations.find(accountKey);
    if (localMatch != session.local_mutations.end()) {
        if (localMatch->second.is_closed) return ExecStatus::KeyMissing;
        outBalance = localMatch->second.staged_balance;
        return ExecStatus::Success;
    }

    // 2. Fetch from historical MVCC ledger
    const AssetVersion* snapshotVersion = fetchSnapshot(accountKey, session.view_timestamp);
    if (snapshotVersion == nullptr || snapshotVersion->is_closed) {
        return ExecStatus::KeyMissing;
    }
    
    outBalance = snapshotVersion->balance;
    return ExecStatus::Success;
}

ExecStatus LedgerCoordinator::requestWriteLock(LedgerSession& session, const std::string& key) {
    // Already holding the lock?
    if (session.held_locks.count(key) > 0) {
        return ExecStatus::Success;
    }

    // Is the lock available?
    auto lockEntry = exclusive_locks_.find(key);
    if (lockEntry == exclusive_locks_.end()) {
        exclusive_locks_[key] = session.session_id;
        session.held_locks.insert(key);
        return ExecStatus::Success;
    }

    TxID currentOwner = lockEntry->second;
    
    // Safety check: is it already ours?
    if (currentOwner == session.session_id) {
        session.held_locks.insert(key);
        return ExecStatus::Success;
    }

    // Lock contention. Build Wait-For Graph dependency
    wait_for_graph_[session.session_id] = currentOwner;

    // Detect Circular Deadlocks (DFS traversal)
    std::vector<TxID> traversalPath;
    std::unordered_set<TxID> visitedNodes;
    TxID currentNode = currentOwner;
    bool cyclicDeadlockFound = false;

    while (true) {
        traversalPath.push_back(currentNode);
        
        // Did we wrap back around to the requesting session?
        if (currentNode == session.session_id) { 
            cyclicDeadlockFound = true; 
            break; 
        }
        
        // Reached an already checked node without finding the requestor
        if (visitedNodes.count(currentNode) > 0) {
            break;
        }

        visitedNodes.insert(currentNode);
        auto nextDependency = wait_for_graph_.find(currentNode);
        if (nextDependency == wait_for_graph_.end()) {
            break; // No further dependencies
        }
        currentNode = nextDependency->second;
    }

    if (!cyclicDeadlockFound) {
        return ExecStatus::WaitBlocked;
    }

    // Resolve deadlock by aborting the youngest transaction in the cycle (highest TxID)
    TxID victimCandidate = 0;
    for (TxID nodeInCycle : traversalPath) {
        if (nodeInCycle > victimCandidate) {
            victimCandidate = nodeInCycle;
        }
    }
    
    latest_victim_tx_ = victimCandidate;
    executeTeardown(victimCandidate);

    // If we killed ourselves, report aborted
    if (victimCandidate == session.session_id) {
        return ExecStatus::ForceAborted;
    }

    // If we killed the blocker, we can attempt to acquire the lock again
    wait_for_graph_.erase(session.session_id);
    auto reassessLock = exclusive_locks_.find(key);
    
    if (reassessLock == exclusive_locks_.end()) {
        exclusive_locks_[key] = session.session_id;
        session.held_locks.insert(key);
        return ExecStatus::Success;
    }

    // Lock was transferred to someone else? Rebuild dependency
    wait_for_graph_[session.session_id] = reassessLock->second;
    return ExecStatus::WaitBlocked;
}

void LedgerCoordinator::dropAllLocks(LedgerSession& session) {
    for (const std::string& assetKey : session.held_locks) {
        auto activeLock = exclusive_locks_.find(assetKey);
        if (activeLock != exclusive_locks_.end() && activeLock->second == session.session_id) {
            exclusive_locks_.erase(activeLock);
        }
    }
    session.held_locks.clear();
}

void LedgerCoordinator::clearWaitGraphEdges(TxID tid) {
    auto edgeIter = wait_for_graph_.begin();
    while (edgeIter != wait_for_graph_.end()) {
        if (edgeIter->first == tid || edgeIter->second == tid) {
            edgeIter = wait_for_graph_.erase(edgeIter);
        } else {
            ++edgeIter;
        }
    }
}

ExecStatus LedgerCoordinator::updateRecord(TxID tid, const std::string& accountKey, const std::string& newBalance) {
    LedgerSession& session = active_sessions_.at(tid);
    if (session.state != Lifecycle::InProgress) return ExecStatus::ForceAborted;

    ExecStatus lockOutcome = requestWriteLock(session, accountKey);
    if (lockOutcome != ExecStatus::Success) return lockOutcome;

    session.local_mutations[accountKey] = StagedMutation{newBalance, false};
    return ExecStatus::Success;
}

ExecStatus LedgerCoordinator::deleteRecord(TxID tid, const std::string& accountKey) {
    LedgerSession& session = active_sessions_.at(tid);
    if (session.state != Lifecycle::InProgress) return ExecStatus::ForceAborted;

    ExecStatus lockOutcome = requestWriteLock(session, accountKey);
    if (lockOutcome != ExecStatus::Success) return lockOutcome;

    bool recordExists = false;
    auto localSearch = session.local_mutations.find(accountKey);
    
    if (localSearch != session.local_mutations.end()) {
        recordExists = !localSearch->second.is_closed;
    } else {
        recordExists = (fetchSnapshot(accountKey, session.view_timestamp) != nullptr);
    }

    if (!recordExists) return ExecStatus::KeyMissing;

    session.local_mutations[accountKey] = StagedMutation{"", true};
    return ExecStatus::Success;
}

ExecStatus LedgerCoordinator::commitTransaction(TxID tid) {
    LedgerSession& session = active_sessions_.at(tid);
    if (session.state != Lifecycle::InProgress) return ExecStatus::ForceAborted;

    // First-Updater-Wins Validation: Ensure no concurrent commits updated our modified keys
    for (const auto& mutationData : session.local_mutations) {
        auto ledgerEntry = historical_ledger_.find(mutationData.first);
        if (ledgerEntry == historical_ledger_.end()) continue;
        
        for (const AssetVersion& historicalVer : ledgerEntry->second) {
            // If another transaction created a new version AFTER our snapshot started, conflict!
            if (historicalVer.mint_timestamp > session.view_timestamp) {
                executeTeardown(tid);
                return ExecStatus::CommitConflict;
            }
        }
    }

    // Validation passed. Advance global clock and permanently record mutations
    TxID commitTimestamp = ++global_clock_;
    
    for (const auto& mutationData : session.local_mutations) {
        std::vector<AssetVersion>& assetTimeline = historical_ledger_[mutationData.first];
        
        // Expire the currently active version
        for (AssetVersion& historicalVer : assetTimeline) {
            if (historicalVer.burn_timestamp == 0) {
                historicalVer.burn_timestamp = commitTimestamp;
            }
        }
        
        // Mint the new version
        AssetVersion newlyMinted;
        newlyMinted.balance = mutationData.second.staged_balance;
        newlyMinted.is_closed = mutationData.second.is_closed;
        newlyMinted.mint_timestamp = commitTimestamp;
        newlyMinted.burn_timestamp = 0; // Active
        newlyMinted.origin_tx = tid;
        
        assetTimeline.push_back(std::move(newlyMinted));
    }

    dropAllLocks(session);
    clearWaitGraphEdges(tid);
    session.state = Lifecycle::Finalized;
    
    return ExecStatus::Success;
}

void LedgerCoordinator::executeTeardown(TxID tid) {
    auto sessionIter = active_sessions_.find(tid);
    if (sessionIter == active_sessions_.end()) return;
    
    LedgerSession& session = sessionIter->second;
    if (session.state != Lifecycle::InProgress) return;

    dropAllLocks(session);
    clearWaitGraphEdges(tid);
    session.local_mutations.clear();
    session.state = Lifecycle::RolledBack;
}

void LedgerCoordinator::rollbackTransaction(TxID tid) {
    executeTeardown(tid);
}

std::size_t LedgerCoordinator::collectGarbage() {
    // Find the oldest snapshot view currently active in the system
    TxID safePurgeHorizon = global_clock_;
    
    for (const auto& sessionPair : active_sessions_) {
        if (sessionPair.second.state == Lifecycle::InProgress) {
            if (sessionPair.second.view_timestamp < safePurgeHorizon) {
                safePurgeHorizon = sessionPair.second.view_timestamp;
            }
        }
    }

    std::size_t sweptAssetsCount = 0;
    
    for (auto& ledgerPair : historical_ledger_) {
        std::vector<AssetVersion>& timeline = ledgerPair.second;
        
        auto obsoleteEnd = std::remove_if(timeline.begin(), timeline.end(), [&](const AssetVersion& ver) {
            // A version is obsolete if it was burned/replaced before the oldest active transaction started
            return (ver.burn_timestamp != 0) && (ver.burn_timestamp <= safePurgeHorizon);
        });
        
        sweptAssetsCount += static_cast<std::size_t>(std::distance(obsoleteEnd, timeline.end()));
        timeline.erase(obsoleteEnd, timeline.end());
    }
    
    return sweptAssetsCount;
}

Lifecycle LedgerCoordinator::queryTxLifecycle(TxID tid) const {
    auto sessionIter = active_sessions_.find(tid);
    if (sessionIter == active_sessions_.end()) return Lifecycle::RolledBack;
    return sessionIter->second.state;
}

std::size_t LedgerCoordinator::getTotalStoredVersions() const {
    std::size_t total = 0;
    for (const auto& ledgerPair : historical_ledger_) {
        total += ledgerPair.second.size();
    }
    return total;
}

} // namespace mvcc_ledger