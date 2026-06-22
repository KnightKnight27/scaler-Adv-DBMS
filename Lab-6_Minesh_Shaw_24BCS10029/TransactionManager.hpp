#pragma once

#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>

enum class TxnState { ACTIVE, COMMITTED, ABORTED };

struct Version {
    int xmin;          
    int xmax;   
    int data;          
};

struct Transaction {
    int txn_id;
    TxnState state;
    int snapshot_xid;  
    std::unordered_set<int> locks_held; 
};

class TransactionManager {
private:
    int global_xid_counter;
    
    std::unordered_map<int, std::vector<Version>> table;
    
    std::unordered_map<int, Transaction> transactions;

    std::unordered_map<int, int> exclusive_locks;
    
    std::unordered_map<int, std::unordered_set<int>> waits_for;

    bool IsVisible(const Version& v, int txn_id);
    bool DetectDeadlockHelper(int current, std::unordered_set<int>& visited, std::unordered_set<int>& recursion_stack);
    bool DetectDeadlock();
    void AbortInternal(int txn_id);

public:
    TransactionManager();

    int Begin();
    void Commit(int txn_id);
    void Abort(int txn_id);

    bool Read(int txn_id, int row_id, int& out_value);
    bool Write(int txn_id, int row_id, int value);
};