// transaction_engine.cpp — ADBMS Lab 8, 24bcs10213 Jatin Chulet
//
// Core logic for the MVCC + strict-2PL transaction engine.

#include "transaction_engine.h"
#include <algorithm>

namespace db_core {

const char* status_to_string(OpStatus st) {
    switch (st) {
        case OpStatus::Success:       return "SUCCESS";
        case OpStatus::Missing:       return "MISSING";
        case OpStatus::Waiting:       return "WAITING";
        case OpStatus::RolledBack:    return "ROLLED_BACK";
        case OpStatus::ConflictError: return "CONFLICT_ERROR";
    }
    return "UNKNOWN";
}

TransactionID TransactionEngine::start_tx() {
    TransactionID new_id = id_generator_++;
    TransactionContext ctx;
    ctx.tx_id = new_id;
    ctx.view_ts = global_timer_;
    ctx.current_status = State::Running;
    active_transactions_.emplace(new_id, std::move(ctx));
    return new_id;
}

const TransactionEngine::RecordVersion* TransactionEngine::find_visible_version(const std::string& rec_key, TransactionID view_ts) const {
    auto record_it = data_store_.find(rec_key);
    if (record_it == data_store_.end()) return nullptr;

    for (const RecordVersion& rev : record_it->second) {
        bool is_created = rev.start_time <= view_ts;
        bool is_active = (rev.end_time == 0) || (rev.end_time > view_ts);
        if (is_created && is_active) return &rev;
    }
    return nullptr;
}

OpStatus TransactionEngine::fetch_record(TransactionID tid, const std::string& rec_key, std::string& result) {
    TransactionContext& ctx = active_transactions_.at(tid);
    if (ctx.current_status != State::Running) return OpStatus::RolledBack;

    auto local_op = ctx.pending_ops.find(rec_key);
    if (local_op != ctx.pending_ops.end()) {
        if (local_op->second.is_tombstone) return OpStatus::Missing;
        result = local_op->second.data;
        return OpStatus::Success;
    }

    const RecordVersion* ver = find_visible_version(rec_key, ctx.view_ts);
    if (!ver || ver->is_tombstone) return OpStatus::Missing;
    result = ver->data;
    return OpStatus::Success;
}

OpStatus TransactionEngine::try_lock(TransactionContext& ctx, const std::string& rec_key) {
    if (ctx.held_locks.count(rec_key)) return OpStatus::Success;

    auto owner = exclusive_locks_.find(rec_key);
    if (owner == exclusive_locks_.end()) {
        exclusive_locks_[rec_key] = ctx.tx_id;
        ctx.held_locks.insert(rec_key);
        return OpStatus::Success;
    }

    TransactionID lock_owner = owner->second;
    if (lock_owner == ctx.tx_id) {
        ctx.held_locks.insert(rec_key);
        return OpStatus::Success;
    }

    wait_graph_[ctx.tx_id] = lock_owner;

    std::vector<TransactionID> wait_path;
    std::unordered_set<TransactionID> visited;
    TransactionID current_node = lock_owner;
    bool has_deadlock = false;

    while (true) {
        wait_path.push_back(current_node);
        if (current_node == ctx.tx_id) { has_deadlock = true; break; }
        if (visited.count(current_node)) break;

        visited.insert(current_node);
        auto next_wait = wait_graph_.find(current_node);
        if (next_wait == wait_graph_.end()) break;
        current_node = next_wait->second;
    }

    if (!has_deadlock) return OpStatus::Waiting;

    TransactionID abort_target = 0;
    for (TransactionID node : wait_path) abort_target = std::max(abort_target, node);
    latest_aborted_ = abort_target;
    force_rollback(abort_target);

    if (abort_target == ctx.tx_id) return OpStatus::RolledBack;

    wait_graph_.erase(ctx.tx_id);
    auto check_lock = exclusive_locks_.find(rec_key);
    if (check_lock == exclusive_locks_.end()) {
        exclusive_locks_[rec_key] = ctx.tx_id;
        ctx.held_locks.insert(rec_key);
        return OpStatus::Success;
    }

    wait_graph_[ctx.tx_id] = check_lock->second;
    return OpStatus::Waiting;
}

void TransactionEngine::free_locks(TransactionContext& ctx) {
    for (const std::string& key : ctx.held_locks) {
        auto locked_item = exclusive_locks_.find(key);
        if (locked_item != exclusive_locks_.end() && locked_item->second == ctx.tx_id) {
            exclusive_locks_.erase(locked_item);
        }
    }
    ctx.held_locks.clear();
}

void TransactionEngine::clear_wait_edges(TransactionID tid) {
    for (auto iter = wait_graph_.begin(); iter != wait_graph_.end(); ) {
        if (iter->first == tid || iter->second == tid) {
            iter = wait_graph_.erase(iter);
        } else {
            ++iter;
        }
    }
}

OpStatus TransactionEngine::update_record(TransactionID tid, const std::string& rec_key, const std::string& val) {
    TransactionContext& ctx = active_transactions_.at(tid);
    if (ctx.current_status != State::Running) return OpStatus::RolledBack;

    OpStatus lock_res = try_lock(ctx, rec_key);
    if (lock_res != OpStatus::Success) return lock_res;

    ctx.pending_ops[rec_key] = BufferedWrite{val, false};
    return OpStatus::Success;
}

OpStatus TransactionEngine::delete_record(TransactionID tid, const std::string& rec_key) {
    TransactionContext& ctx = active_transactions_.at(tid);
    if (ctx.current_status != State::Running) return OpStatus::RolledBack;

    OpStatus lock_res = try_lock(ctx, rec_key);
    if (lock_res != OpStatus::Success) return lock_res;

    auto pending = ctx.pending_ops.find(rec_key);
    bool valid = (pending != ctx.pending_ops.end()) ? !pending->second.is_tombstone
                                                    : (find_visible_version(rec_key, ctx.view_ts) != nullptr);
    if (!valid) return OpStatus::Missing;

    ctx.pending_ops[rec_key] = BufferedWrite{std::string(), true};
    return OpStatus::Success;
}

OpStatus TransactionEngine::commit_tx(TransactionID tid) {
    TransactionContext& ctx = active_transactions_.at(tid);
    if (ctx.current_status != State::Running) return OpStatus::RolledBack;

    for (const auto& operation : ctx.pending_ops) {
        auto store_it = data_store_.find(operation.first);
        if (store_it == data_store_.end()) continue;
        for (const RecordVersion& rev : store_it->second) {
            if (rev.start_time > ctx.view_ts) {
                force_rollback(tid);
                return OpStatus::ConflictError;
            }
        }
    }

    TransactionID commit_time = ++global_timer_;
    for (const auto& operation : ctx.pending_ops) {
        std::vector<RecordVersion>& history = data_store_[operation.first];
        for (RecordVersion& rev : history) {
            if (rev.end_time == 0) rev.end_time = commit_time;
        }
        RecordVersion new_rev;
        new_rev.data = operation.second.data;
        new_rev.is_tombstone = operation.second.is_tombstone;
        new_rev.start_time = commit_time;
        new_rev.end_time = 0;
        new_rev.author_tx = tid;
        history.push_back(std::move(new_rev));
    }

    free_locks(ctx);
    clear_wait_edges(tid);
    ctx.current_status = State::Saved;
    return OpStatus::Success;
}

void TransactionEngine::force_rollback(TransactionID tid) {
    auto map_it = active_transactions_.find(tid);
    if (map_it == active_transactions_.end()) return;
    TransactionContext& ctx = map_it->second;
    if (ctx.current_status != State::Running) return;

    free_locks(ctx);
    clear_wait_edges(tid);
    ctx.pending_ops.clear();
    ctx.current_status = State::Failed;
}

void TransactionEngine::rollback_tx(TransactionID tid) {
    force_rollback(tid);
}

std::size_t TransactionEngine::cleanup_garbage() {
    TransactionID cutoff_time = global_timer_;
    for (const auto& pair : active_transactions_) {
        if (pair.second.current_status == State::Running) {
            cutoff_time = std::min(cutoff_time, pair.second.view_ts);
        }
    }

    std::size_t removed_count = 0;
    for (auto& pair : data_store_) {
        std::vector<RecordVersion>& history = pair.second;
        auto obsolete = std::remove_if(history.begin(), history.end(), [&](const RecordVersion& rev) {
            return rev.end_time != 0 && rev.end_time <= cutoff_time;
        });
        removed_count += static_cast<std::size_t>(std::distance(obsolete, history.end()));
        history.erase(obsolete, history.end());
    }
    return removed_count;
}

State TransactionEngine::get_state(TransactionID tid) const {
    auto map_it = active_transactions_.find(tid);
    return map_it == active_transactions_.end() ? State::Failed : map_it->second.current_status;
}

std::size_t TransactionEngine::get_total_versions() const {
    std::size_t total = 0;
    for (const auto& pair : data_store_) total += pair.second.size();
    return total;
}

}  // namespace db_core