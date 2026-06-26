#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <string>

using TxnIdentifier = uint64_t;
using DatabaseKey = std::string;

enum class TransactionState { Active, Committed, Aborted };

struct TxnDescriptor {
    TxnIdentifier tx_id;
    TxnIdentifier visibility_epoch;
    TransactionState execution_state = TransactionState::Active;
    bool holds_lock_contract = true;
};

static std::atomic<TxnIdentifier> global_txn_counter{1};
static std::mutex txn_registry_mutex;
static std::unordered_map<TxnIdentifier, TxnDescriptor> txn_registry;

TxnIdentifier register_new_transaction() {
    std::lock_guard<std::mutex> lock(txn_registry_mutex);
    TxnIdentifier next_id = global_txn_counter.fetch_add(1);
    txn_registry[next_id] = {next_id, next_id, TransactionState::Active, true};
    return next_id;
}

bool check_txn_committed(TxnIdentifier tx_id) {
    std::lock_guard<std::mutex> lock(txn_registry_mutex);
    auto it = txn_registry.find(tx_id);
    return it != txn_registry.end() && it->second.execution_state == TransactionState::Committed;
}

struct TupleRevision {
    std::string payload;
    TxnIdentifier insertion_tx;
    TxnIdentifier deletion_tx;
};

static std::mutex storage_heap_mutex;
static std::unordered_map<DatabaseKey, std::list<TupleRevision>> storage_heap;

bool evaluate_version_visibility(const TupleRevision& revision, TxnIdentifier snapshot, TxnIdentifier reader) {
    bool is_inserted_visible = (revision.insertion_tx == reader) || 
                               (check_txn_committed(revision.insertion_tx) && revision.insertion_tx < snapshot);

    if (!is_inserted_visible) return false;
    if (revision.deletion_tx == 0) return true;

    bool is_deleted_visible = (revision.deletion_tx == reader) || 
                              (check_txn_committed(revision.deletion_tx) && revision.deletion_tx < snapshot);

    return !is_deleted_visible;
}

std::optional<std::string> read_mvcc_record(const DatabaseKey& key, TxnIdentifier tx_id) {
    std::lock_guard<std::mutex> heap_lock(storage_heap_mutex);
    TxnIdentifier current_snapshot;
    {
        std::lock_guard<std::mutex> registry_lock(txn_registry_mutex);
        current_snapshot = txn_registry[tx_id].visibility_epoch;
    }

    auto map_it = storage_heap.find(key);
    if (map_it == storage_heap.end()) return std::nullopt;

    for (const auto& revision : map_it->second) {
        if (evaluate_version_visibility(revision, current_snapshot, tx_id)) {
            return revision.payload;
        }
    }
    return std::nullopt;
}

void insert_mvcc_record(const DatabaseKey& key, const std::string& value, TxnIdentifier tx_id) {
    std::lock_guard<std::mutex> heap_lock(storage_heap_mutex);
    storage_heap[key].push_front({value, tx_id, 0});
}

void update_mvcc_record(const DatabaseKey& key, const std::string& value, TxnIdentifier tx_id) {
    std::lock_guard<std::mutex> heap_lock(storage_heap_mutex);
    TxnIdentifier current_snapshot;
    {
        std::lock_guard<std::mutex> registry_lock(txn_registry_mutex);
        current_snapshot = txn_registry[tx_id].visibility_epoch;
    }

    auto map_it = storage_heap.find(key);
    if (map_it != storage_heap.end()) {
        for (auto& revision : map_it->second) {
            if (evaluate_version_visibility(revision, current_snapshot, tx_id) && revision.deletion_tx == 0) {
                revision.deletion_tx = tx_id;
                break;
            }
        }
    }
    storage_heap[key].push_front({value, tx_id, 0});
}

void delete_mvcc_record(const DatabaseKey& key, TxnIdentifier tx_id) {
    std::lock_guard<std::mutex> heap_lock(storage_heap_mutex);
    TxnIdentifier current_snapshot;
    {
        std::lock_guard<std::mutex> registry_lock(txn_registry_mutex);
        current_snapshot = txn_registry[tx_id].visibility_epoch;
    }

    auto map_it = storage_heap.find(key);
    if (map_it == storage_heap.end()) return;

    for (auto& revision : map_it->second) {
        if (evaluate_version_visibility(revision, current_snapshot, tx_id) && revision.deletion_tx == 0) {
            revision.deletion_tx = tx_id;
            return;
        }
    }
}

enum class LockIntent { Shared, Exclusive };

struct SubscriptionRequest {
    TxnIdentifier request_tx_id;
    LockIntent access_intent;
    bool is_granted = false;
};

struct ResourceLockChannel {
    std::list<SubscriptionRequest> waiting_line;
    std::mutex channel_mutex;
    std::condition_variable dynamic_signal;
};

static std::mutex manager_graph_mutex;
static std::unordered_map<DatabaseKey, ResourceLockChannel> lock_directory;
static std::unordered_map<TxnIdentifier, std::unordered_set<TxnIdentifier>> transaction_dependency_graph;

class ConcurrencyDeadlockException : public std::runtime_error {
public:
    ConcurrencyDeadlockException(TxnIdentifier tx_id)
        : std::runtime_error("Deadlock validation exception for transaction reference: " + std::to_string(tx_id)) {}
};

bool evaluate_dependency_cycles(TxnIdentifier current_node, 
                                 std::unordered_map<TxnIdentifier, std::unordered_set<TxnIdentifier>>& graph,
                                 std::unordered_set<TxnIdentifier>& total_visited, 
                                 std::unordered_set<TxnIdentifier>& evaluation_stack) {
    total_visited.insert(current_node);
    evaluation_stack.insert(current_node);

    auto graph_it = graph.find(current_node);
    if (graph_it != graph.end()) {
        for (auto contiguous_node : graph_it->second) {
            if (total_visited.find(contiguous_node) == total_visited.end()) {
                if (evaluate_dependency_cycles(contiguous_node, graph, total_visited, evaluation_stack)) {
                    return true;
                }
            } else if (evaluation_stack.find(contiguous_node) != evaluation_stack.end()) {
                return true;
            }
        }
    }

    evaluation_stack.erase(current_node);
    return false;
}

bool discovers_active_cycle(TxnIdentifier root_node, std::unordered_map<TxnIdentifier, std::unordered_set<TxnIdentifier>>& graph) {
    std::unordered_set<TxnIdentifier> total_visited, evaluation_stack;
    return evaluate_dependency_cycles(root_node, graph, total_visited, evaluation_stack);
}

void block_and_acquire(const DatabaseKey& key, TxnIdentifier tx_id, LockIntent intent) {
    ResourceLockChannel& channel = lock_directory[key];
    std::unique_lock<std::mutex> local_lock(channel.channel_mutex);

    channel.waiting_line.push_back({tx_id, intent, false});
    auto current_position = std::prev(channel.waiting_line.end());

    while (true) {
        bool operational_conflict = false;
        std::unordered_set<TxnIdentifier> obstructing_transactions;

        for (auto& request : channel.waiting_line) {
            if (&request == &(*current_position)) break;
            if (!request.is_granted) continue;

            if (intent == LockIntent::Exclusive || request.access_intent == LockIntent::Exclusive) {
                operational_conflict = true;
                obstructing_transactions.insert(request.request_tx_id);
            }
        }

        if (!operational_conflict) {
            current_position->is_granted = true;
            std::lock_guard<std::mutex> graph_lock(manager_graph_mutex);
            transaction_dependency_graph.erase(tx_id);
            return;
        }

        {
            std::lock_guard<std::mutex> graph_lock(manager_graph_mutex);
            transaction_dependency_graph[tx_id] = obstructing_transactions;

            if (discovers_active_cycle(tx_id, transaction_dependency_graph)) {
                channel.waiting_line.erase(current_position);
                throw ConcurrencyDeadlockException(tx_id);
            }
        }

        channel.dynamic_signal.wait(local_lock);
    }
}

void discharge_transaction_locks(TxnIdentifier tx_id) {
    for (auto& [resource_key, channel] : lock_directory) {
        std::lock_guard<std::mutex> channel_lock(channel.channel_mutex);
        channel.waiting_line.remove_if([&](const SubscriptionRequest& req) { return req.request_tx_id == tx_id; });
        channel.dynamic_signal.notify_all();
    }

    std::lock_guard<std::mutex> graph_lock(manager_graph_mutex);
    transaction_dependency_graph.erase(tx_id);
}

class IsolationCoordinator {
public:
    TxnIdentifier allocate_transaction() {
        return register_new_transaction();
    }

    std::optional<std::string> read_transactional(TxnIdentifier tx_id, const DatabaseKey& key) {
        block_and_acquire(key, tx_id, LockIntent::Shared);
        return read_mvcc_record(key, tx_id);
    }

    void insert_transactional(TxnIdentifier tx_id, const DatabaseKey& key, const std::string& value) {
        block_and_acquire(key, tx_id, LockIntent::Exclusive);
        insert_mvcc_record(key, value, tx_id);
    }

    void update_transactional(TxnIdentifier tx_id, const DatabaseKey& key, const std::string& value) {
        block_and_acquire(key, tx_id, LockIntent::Exclusive);
        update_mvcc_record(key, value, tx_id);
    }

    void remove_transactional(TxnIdentifier tx_id, const DatabaseKey& key) {
        block_and_acquire(key, tx_id, LockIntent::Exclusive);
        delete_mvcc_record(key, tx_id);
    }

    void finalize_commit(TxnIdentifier tx_id) {
        {
            std::lock_guard<std::mutex> registry_lock(txn_registry_mutex);
            txn_registry[tx_id].execution_state = TransactionState::Committed;
        }
        discharge_transaction_locks(tx_id);
        std::cout << "[Transaction ID: " << tx_id << "] Execution Committed Successfully\n";
    }

    void process_abort(TxnIdentifier tx_id) {
        {
            std::lock_guard<std::mutex> heap_lock(storage_heap_mutex);
            for (auto& [key, chain] : storage_heap) {
                for (auto& revision : chain) {
                    if (revision.insertion_tx == tx_id) revision.deletion_tx = tx_id;
                    if (revision.deletion_tx == tx_id) revision.deletion_tx = 0;
                }
            }
        }

        {
            std::lock_guard<std::mutex> registry_lock(txn_registry_mutex);
            txn_registry[tx_id].execution_state = TransactionState::Aborted;
        }

        discharge_transaction_locks(tx_id);
        std::cout << "[Transaction ID: " << tx_id << "] Execution Aborted Gracefully\n";
    }
};

int main() {
    IsolationCoordinator coordinator;

    TxnIdentifier transaction_1 = coordinator.allocate_transaction();
    coordinator.insert_transactional(transaction_1, "account_balance", "5000");
    coordinator.finalize_commit(transaction_1);

    TxnIdentifier transaction_2 = coordinator.allocate_transaction();
    TxnIdentifier transaction_3 = coordinator.allocate_transaction();

    coordinator.update_transactional(transaction_3, "account_balance", "7500");
    coordinator.finalize_commit(transaction_3);

    std::optional<std::string> visible_value = coordinator.read_transactional(transaction_2, "account_balance");
    std::cout << "Read Value: " << (visible_value ? *visible_value : "None") << "\n";
    coordinator.finalize_commit(transaction_2);

    return 0;
}