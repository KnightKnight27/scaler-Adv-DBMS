#include "TransactionManager.hpp"
#include <algorithm>



// Lifecycle Management

TransactionManager::TransactionManager() : next_tx_sequence_id(1) {}

int TransactionManager::Begin() {
    int assigned_tx_id = next_tx_sequence_id++;
    
    // Setup initial transaction environment metadata
    tx_registry[assigned_tx_id] = {assigned_tx_id, TransactionState::ACTIVE, assigned_tx_id, {}};
    
    std::cout << "[BEGIN] Spawned Transaction " << assigned_tx_id << "\n";
    return assigned_tx_id;
}



// MVCC Visibility Evaluation

bool TransactionManager::IsRecordVisible(const RecordVersion& version, int reader_tx_id) {
    const auto& context = tx_registry[reader_tx_id];
    bool is_creation_visible = false;

    // Phase 1: Evaluate if record creation is visible to the reading transaction
    if (version.created_by_tx == reader_tx_id) {
        is_creation_visible = true; 
    } else if (tx_registry.count(version.created_by_tx) && 
               tx_registry[version.created_by_tx].state == TransactionState::COMMITTED && 
               version.created_by_tx <= context.read_view_watermark) {
        is_creation_visible = true;
    }

    if (!is_creation_visible) return false;

    // Phase 2: Evaluate if record deletion hides this version
    if (version.deleted_by_tx == 0) return true;
    if (version.deleted_by_tx == reader_tx_id) return false; // Self-deleted records are invisible
    
    if (tx_registry.count(version.deleted_by_tx)) {
        if (tx_registry[version.deleted_by_tx].state == TransactionState::ABORTED) return true;
        if (tx_registry[version.deleted_by_tx].state == TransactionState::COMMITTED && 
            version.deleted_by_tx <= context.read_view_watermark) {
            return false;
        }
    }

    return true;
}



// Read / Write Record Operations

bool TransactionManager::Read(int tx_id, int row_id, int& out_value) {
    if (tx_registry[tx_id].state != TransactionState::ACTIVE) {
        std::cerr << "[ERROR] Txn " << tx_id << " is not in an active state.\n";
        return false;
    }

    if (storage_engine.find(row_id) != storage_engine.end()) {
        // Reverse iteration: check newest versions first
        for (auto rit = storage_engine[row_id].rbegin(); rit != storage_engine[row_id].rend(); ++rit) {
            if (IsRecordVisible(*rit, tx_id)) {
                out_value = rit->payload;
                std::cout << "[READ] Txn " << tx_id << " -> Row " << row_id 
                          << " = " << out_value << " (Created By Txn=" << rit->created_by_tx << ")\n";
                return true;
            }
        }
    }
    
    std::cout << "[READ] Txn " << tx_id << " -> No valid MVCC version visible for Row " << row_id << ".\n";
    return false; 
}

bool TransactionManager::Write(int tx_id, int row_id, int value) {
    if (tx_registry[tx_id].state != TransactionState::ACTIVE) return false;

    // Structural Lock-Conflict Checking
    if (exclusive_row_locks.count(row_id) && exclusive_row_locks[row_id] != tx_id) {
        int holding_tx_id = exclusive_row_locks[row_id];
        std::cout << "[LOCK CONFLICT] Txn " << tx_id << " is blocked by Txn " 
                  << holding_tx_id << " on Row " << row_id << ".\n";
        
        dependency_graph[tx_id].insert(holding_tx_id);

        // Terminate execution branch if an unrecoverable deadlock loop forms
        if (IsDeadlockDetected()) {
            std::cout << "[DEADLOCK] Cycle resolved! Forced abort on youngest transaction: " << tx_id << "\n";
            ExecuteInternalRollback(tx_id);
            return false;
        }
        return false;
    }

    // Register exclusive ownership rights
    exclusive_row_locks[row_id] = tx_id;
    tx_registry[tx_id].acquired_locks.insert(row_id);

    // Apply soft deletion mark (deleted_by_tx) to the prior active version chain
    if (storage_engine.find(row_id) != storage_engine.end()) {
        for (auto& version : storage_engine[row_id]) {
            if (version.deleted_by_tx == 0 && IsRecordVisible(version, tx_id)) {
                version.deleted_by_tx = tx_id; 
                break;
            }
        }
    }
    
    // Append the newly written data point version
    storage_engine[row_id].push_back({tx_id, 0, value});
    std::cout << "[WRITE] Txn " << tx_id << " wrote data to Row " << row_id 
              << " = " << value << " (Exclusive Lock Registered).\n";
    return true;
}



// Transaction Termination Logic

void TransactionManager::Commit(int tx_id) {
    if (tx_registry[tx_id].state != TransactionState::ACTIVE) return;

    tx_registry[tx_id].state = TransactionState::COMMITTED;
    std::cout << "[COMMIT] Txn " << tx_id << " successfully completed.\n";

    // Clean up acquired transaction environment resources
    for (int row_id : tx_registry[tx_id].acquired_locks) {
        exclusive_row_locks.erase(row_id);
        std::cout << "        -> Freed exclusive lock on Row " << row_id << "\n";
    }
    tx_registry[tx_id].acquired_locks.clear();
    
    // Clear out blocking elements from historical graph tracking
    dependency_graph.erase(tx_id);
    for (auto& structural_node : dependency_graph) {
        structural_node.second.erase(tx_id);
    }
}

void TransactionManager::ExecuteInternalRollback(int tx_id) {
    tx_registry[tx_id].state = TransactionState::ABORTED;
    std::cout << "[ABORT] Txn " << tx_id << " halted. Undoing historical modifications.\n";

    // Revert speculative table structure updates
    for (auto& map_entry : storage_engine) {
        auto& version_chain = map_entry.second;

        // Undo transactional update indicators
        for (auto& version : version_chain) {
            if (version.deleted_by_tx == tx_id) version.deleted_by_tx = 0;
        }

        // Purge invalid temporary uncommitted data elements
        version_chain.erase(
            std::remove_if(version_chain.begin(), version_chain.end(), 
                [tx_id](const RecordVersion& v) { return v.created_by_tx == tx_id; }), 
            version_chain.end()
        );
    }

    // Relinquish remaining control items
    for (int row_id : tx_registry[tx_id].acquired_locks) {
        exclusive_row_locks.erase(row_id);
    }
    tx_registry[tx_id].acquired_locks.clear();

    dependency_graph.erase(tx_id);
    for (auto& structural_node : dependency_graph) {
        structural_node.second.erase(tx_id);
    }
}

void TransactionManager::Abort(int tx_id) {
    if (tx_registry[tx_id].state == TransactionState::ACTIVE) {
        ExecuteInternalRollback(tx_id);
    }
}



// Deadlock Validation Engine (DFS Cycle Tracking)

bool TransactionManager::TraceDeadlockCycle(int current_tx, std::unordered_set<int>& visited_nodes, std::unordered_set<int>& active_recursion_stack) {
    visited_nodes.insert(current_tx);
    active_recursion_stack.insert(current_tx);

    for (int adj_node : dependency_graph[current_tx]) {
        if (active_recursion_stack.count(adj_node)) {
            return true; // Loop discovered back toward stack baseline
        }
        if (!visited_nodes.count(adj_node)) {
            if (TraceDeadlockCycle(adj_node, visited_nodes, active_recursion_stack)) {
                return true;
            }
        }
    }

    active_recursion_stack.erase(current_tx);
    return false;
}

bool TransactionManager::IsDeadlockDetected() {
    std::unordered_set<int> visited_nodes;
    std::unordered_set<int> active_recursion_stack;

    for (const auto& mapping : dependency_graph) {
        if (!visited_nodes.count(mapping.first)) {
            if (TraceDeadlockCycle(mapping.first, visited_nodes, active_recursion_stack)) {
                return true;
            }
        }
    }
    return false;
}