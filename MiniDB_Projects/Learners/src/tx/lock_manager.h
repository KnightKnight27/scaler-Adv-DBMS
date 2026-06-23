#ifndef LOCK_MANAGER_H
#define LOCK_MANAGER_H

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <stdexcept>
#include <memory>
#include "../compat.h"

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(const std::string& msg) : std::runtime_error(msg) {}
};

struct LockRequest {
    int txn_id;
    char lock_type; // 'S' or 'X'
    bool granted{false};
    std::shared_ptr<CondVar> cv;

    LockRequest(int txn_id, char type) 
        : txn_id(txn_id), lock_type(type), cv(std::make_shared<CondVar>()) {}
};

struct LockEntry {
    std::unordered_map<int, char> holders; // txn_id -> 'S' or 'X'
    std::vector<std::shared_ptr<LockRequest>> waiters;
};

class LockManager {
private:
    Mutex mu;
    // Map resource_id -> LockEntry
    // Resource ID is represented as "table_name|page_id,slot_id"
    std::unordered_map<std::string, LockEntry> lock_table;
    // Map txn_id -> set of resource_ids
    std::unordered_map<int, std::unordered_set<std::string>> txn_locks;

    std::string make_resource_id(const std::string& table, std::pair<int, int> rid) const;
    bool has_deadlock(int start_txn_id);
    void grant_next_waiters(const std::string& resource_id, LockEntry& entry);

public:
    LockManager() = default;
    ~LockManager() = default;

    bool acquire_shared(int txn_id, const std::string& table, std::pair<int, int> rid);
    bool acquire_exclusive(int txn_id, const std::string& table, std::pair<int, int> rid);
    void release_locks(int txn_id);
};

#endif
