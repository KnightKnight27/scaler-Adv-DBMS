// tx_manager.cpp — ADBMS Lab 7, 24bcs10632 Patel Jash
//
// Core logic for the MVCC + strict-2PL transaction engine.

#include "transaction_engine.h"
#include <algorithm>

namespace database_core {

const char* engine_status_to_string(EngineStatus st) {
    switch (st) {
        case EngineStatus::Ok:                 return "OK";
        case EngineStatus::NotFound:           return "NOT_FOUND";
        case EngineStatus::Blocked:            return "BLOCKED";
        case EngineStatus::Aborted:            return "ABORTED";
        case EngineStatus::SerializationError: return "SERIALIZATION_ERROR";
    }
    return "UNKNOWN";
}

TxID TxManager::begin_transaction() {
    TxID new_tid = next_tx_id_++;
    TxContext context;
    context.id = new_tid;
    context.snapshot_time = system_clock_;
    context.status = TxState::Active;
    running_txs_.emplace(new_tid, std::move(context));
    return new_tid;
}

const TxManager::DataVersion* TxManager::locate_visible_version(const std::string& key, TxID view_ts) const {
    auto kv_it = kv_store_.find(key);
    if (kv_it == kv_store_.end()) return nullptr;

    for (const DataVersion& ver : kv_it->second) {
        bool visible_start = ver.created_at <= view_ts;
        bool visible_end = (ver.expired_at == 0) || (ver.expired_at > view_ts);
        if (visible_start && visible_end) return &ver;
    }
    return nullptr;
}

EngineStatus TxManager::read_key(TxID tid, const std::string& key, std::string& out_value) {
    TxContext& ctx = running_txs_.at(tid);
    if (ctx.status != TxState::Active) return EngineStatus::Aborted;

    auto pending_it = ctx.local_writes.find(key);
    if (pending_it != ctx.local_writes.end()) {
        if (pending_it->second.is_deleted) return EngineStatus::NotFound;
        out_value = pending_it->second.value;
        return EngineStatus::Ok;
    }

    const DataVersion* d_ver = locate_visible_version(key, ctx.snapshot_time);
    if (!d_ver || d_ver->is_deleted) return EngineStatus::NotFound;
    out_value = d_ver->value;
    return EngineStatus::Ok;
}

EngineStatus TxManager::attempt_lock(TxContext& ctx, const std::string& key) {
    if (ctx.acquired_locks.count(key)) return EngineStatus::Ok;

    auto current_owner = write_locks_.find(key);
    if (current_owner == write_locks_.end()) {
        write_locks_[key] = ctx.id;
        ctx.acquired_locks.insert(key);
        return EngineStatus::Ok;
    }

    TxID lock_holder = current_owner->second;
    if (lock_holder == ctx.id) {
        ctx.acquired_locks.insert(key);
        return EngineStatus::Ok;
    }

    dependency_graph_[ctx.id] = lock_holder;

    std::vector<TxID> cycle_path;
    std::unordered_set<TxID> visited_nodes;
    TxID traverse_node = lock_holder;
    bool deadlock_found = false;

    while (true) {
        cycle_path.push_back(traverse_node);
        if (traverse_node == ctx.id) { deadlock_found = true; break; }
        if (visited_nodes.count(traverse_node)) break;

        visited_nodes.insert(traverse_node);
        auto next_dep = dependency_graph_.find(traverse_node);
        if (next_dep == dependency_graph_.end()) break;
        traverse_node = next_dep->second;
    }

    if (!deadlock_found) return EngineStatus::Blocked;

    TxID victim = 0;
    for (TxID n : cycle_path) victim = std::max(victim, n);
    last_killed_tx_ = victim;
    execute_rollback(victim);

    if (victim == ctx.id) return EngineStatus::Aborted;

    dependency_graph_.erase(ctx.id);
    auto lock_check = write_locks_.find(key);
    if (lock_check == write_locks_.end()) {
        write_locks_[key] = ctx.id;
        ctx.acquired_locks.insert(key);
        return EngineStatus::Ok;
    }

    dependency_graph_[ctx.id] = lock_check->second;
    return EngineStatus::Blocked;
}

void TxManager::release_all_locks(TxContext& ctx) {
    for (const std::string& k : ctx.acquired_locks) {
        auto locked_entry = write_locks_.find(k);
        if (locked_entry != write_locks_.end() && locked_entry->second == ctx.id) {
            write_locks_.erase(locked_entry);
        }
    }
    ctx.acquired_locks.clear();
}

void TxManager::prune_wait_dependencies(TxID tid) {
    for (auto it = dependency_graph_.begin(); it != dependency_graph_.end(); ) {
        if (it->first == tid || it->second == tid) {
            it = dependency_graph_.erase(it);
        } else {
            ++it;
        }
    }
}

EngineStatus TxManager::write_key(TxID tid, const std::string& key, const std::string& val) {
    TxContext& ctx = running_txs_.at(tid);
    if (ctx.status != TxState::Active) return EngineStatus::Aborted;

    EngineStatus lock_st = attempt_lock(ctx, key);
    if (lock_st != EngineStatus::Ok) return lock_st;

    ctx.local_writes[key] = PendingWrite{val, false};
    return EngineStatus::Ok;
}

EngineStatus TxManager::remove_key(TxID tid, const std::string& key) {
    TxContext& ctx = running_txs_.at(tid);
    if (ctx.status != TxState::Active) return EngineStatus::Aborted;

    EngineStatus lock_st = attempt_lock(ctx, key);
    if (lock_st != EngineStatus::Ok) return lock_st;

    auto pending_match = ctx.local_writes.find(key);
    bool is_valid = (pending_match != ctx.local_writes.end()) ? !pending_match->second.is_deleted
                                                              : (locate_visible_version(key, ctx.snapshot_time) != nullptr);
    if (!is_valid) return EngineStatus::NotFound;

    ctx.local_writes[key] = PendingWrite{std::string(), true};
    return EngineStatus::Ok;
}

EngineStatus TxManager::commit(TxID tid) {
    TxContext& ctx = running_txs_.at(tid);
    if (ctx.status != TxState::Active) return EngineStatus::Aborted;

    for (const auto& op : ctx.local_writes) {
        auto kv_match = kv_store_.find(op.first);
        if (kv_match == kv_store_.end()) continue;
        for (const DataVersion& d_ver : kv_match->second) {
            if (d_ver.created_at > ctx.snapshot_time) {
                execute_rollback(tid);
                return EngineStatus::SerializationError;
            }
        }
    }

    TxID final_commit_time = ++system_clock_;
    for (const auto& op : ctx.local_writes) {
        std::vector<DataVersion>& ver_history = kv_store_[op.first];
        for (DataVersion& d_ver : ver_history) {
            if (d_ver.expired_at == 0) d_ver.expired_at = final_commit_time;
        }
        DataVersion next_ver;
        next_ver.value = op.second.value;
        next_ver.is_deleted = op.second.is_deleted;
        next_ver.created_at = final_commit_time;
        next_ver.expired_at = 0;
        next_ver.writer_id = tid;
        ver_history.push_back(std::move(next_ver));
    }

    release_all_locks(ctx);
    prune_wait_dependencies(tid);
    ctx.status = TxState::Committed;
    return EngineStatus::Ok;
}

void TxManager::execute_rollback(TxID tid) {
    auto tx_it = running_txs_.find(tid);
    if (tx_it == running_txs_.end()) return;
    TxContext& ctx = tx_it->second;
    if (ctx.status != TxState::Active) return;

    release_all_locks(ctx);
    prune_wait_dependencies(tid);
    ctx.local_writes.clear();
    ctx.status = TxState::Terminated;
}

void TxManager::abort_transaction(TxID tid) {
    execute_rollback(tid);
}

std::size_t TxManager::run_vacuum() {
    TxID purge_threshold = system_clock_;
    for (const auto& kv_pair : running_txs_) {
        if (kv_pair.second.status == TxState::Active) {
            purge_threshold = std::min(purge_threshold, kv_pair.second.snapshot_time);
        }
    }

    std::size_t pruned = 0;
    for (auto& kv_pair : kv_store_) {
        std::vector<DataVersion>& history_list = kv_pair.second;
        auto to_remove = std::remove_if(history_list.begin(), history_list.end(), [&](const DataVersion& d_ver) {
            return d_ver.expired_at != 0 && d_ver.expired_at <= purge_threshold;
        });
        pruned += static_cast<std::size_t>(std::distance(to_remove, history_list.end()));
        history_list.erase(to_remove, history_list.end());
    }
    return pruned;
}

TxState TxManager::get_transaction_status(TxID tid) const {
    auto tx_it = running_txs_.find(tid);
    return tx_it == running_txs_.end() ? TxState::Terminated : tx_it->second.status;
}

std::size_t TxManager::count_all_versions() const {
    std::size_t sum = 0;
    for (const auto& kv_pair : kv_store_) sum += kv_pair.second.size();
    return sum;
}

}  // namespace database_core