#pragma once

#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>



// Transaction State & Storage Layout

enum class TransactionState { 
    ACTIVE, 
    COMMITTED, 
    ABORTED 
};

// Represents a single Multi-Version Concurrency Control (MVCC) record snapshot
struct RecordVersion {
    int created_by_tx; // Transaction ID that inserted this version
    int deleted_by_tx; // Transaction ID that deleted/superseded this version (0 if active)
    int payload;       // Actual data value stored
};

// Tracks state and tracking metadata for a single database transaction
struct TxnContext {
    int txn_id;
    TransactionState state;
    int read_view_watermark;               // Snapshot reference for isolation visibility
    std::unordered_set<int> acquired_locks; // Set of row IDs currently locked exclusively
};


// Main Concurrency Engine

class TransactionManager {
private:

    // Storage & State Catalogs
    
    int next_tx_sequence_id; 
    
    // Core database table storage: Row ID -> Chain of record versions
    std::unordered_map<int, std::vector<RecordVersion>> storage_engine;
    
    // Global active/historical transaction tracking log
    std::unordered_map<int, TxnContext> tx_registry;

    
    // Lock Management & Deadlock Detection
    
    // Exclusive lock table: Row ID -> Transaction ID holding the lock
    std::unordered_map<int, int> exclusive_row_locks;
    
    // Waits-For Dependency Graph: Txn ID -> Set of Txn IDs blocking it
    std::unordered_map<int, std::unordered_set<int>> dependency_graph;

    
    // Internal Engine Routines
    
    bool IsRecordVisible(const RecordVersion& version, int reader_tx_id);
    void ExecuteInternalRollback(int tx_id);
    
    // Deadlock cycle detection using Depth-First Search (DFS)
    bool IsDeadlockDetected();
    bool TraceDeadlockCycle(int current_tx, 
                             std::unordered_set<int>& visited_nodes, 
                             std::unordered_set<int>& active_recursion_stack);

public:
    
    // Public Database API

    TransactionManager();

    // Transaction Control Commands
    int Begin();
    void Commit(int tx_id);
    void Abort(int tx_id);

    // Data Access Actions
    bool Read(int tx_id, int row_id, int& out_value);
    bool Write(int tx_id, int row_id, int value);
};