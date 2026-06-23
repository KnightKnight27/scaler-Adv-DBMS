#pragma once

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

namespace minidb {

enum class LockType { SHARED, EXCLUSIVE };
enum class LockMode { NONE, SHARED, EXCLUSIVE };

class LockManager {
public:
    bool acquireLock(int txn_id, const string& resource, LockType type);
    void releaseAllLocks(int txn_id);
    bool detectDeadlock(int waiting_txn);

private:
    struct LockEntry {
        LockMode mode = LockMode::NONE;
        set<int> holders;
        vector<int> wait_queue;
    };
    mutex mutex_;
    unordered_map<string, LockEntry> locks_;
    unordered_map<int, set<string>> txn_locks_;
    unordered_map<int, int> waits_on_;
    bool isCompatible(LockMode held, LockType requested);
    void grantLock(int txn_id, const string& resource, LockType type);
};

}  // namespace minidb
