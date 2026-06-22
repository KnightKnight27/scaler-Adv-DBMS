#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <sstream>
#include <cassert>
#include <functional>

// ---------------------------------------------------------------------
// Concurrent Transaction Manager (MVCC + Strict 2PL + Waits-For Detector)
// Student: Nandani Kumari (24bcs10317)
// Course: Advanced Database Management Systems (ADBMS)
// ---------------------------------------------------------------------

using SessionId = uint64_t;
using TupleName = std::string;

enum class StateInfo { ACTIVE, COMMITTED, ROLLED_BACK };

struct SessionRecord {
    SessionId id;
    SessionId viewSnapshot;
    StateInfo phase = StateInfo::ACTIVE;
    bool lockShrinking = false;
};

static std::atomic<SessionId> atomic_tx_seq{1};
static std::mutex meta_data_mtx;
static std::unordered_map<SessionId, SessionRecord> sessions_directory;

SessionId initializeTransaction() {
    std::lock_guard lk(meta_data_mtx);
    SessionId xid = atomic_tx_seq.fetch_add(1);
    SessionId snap = xid;
    sessions_directory[xid] = SessionRecord{xid, snap, StateInfo::ACTIVE, false};
    return xid;
}

bool checkCommitedState(SessionId xid) {
    std::lock_guard lk(meta_data_mtx);
    auto it = sessions_directory.find(xid);
    return it != sessions_directory.end() && it->second.phase == StateInfo::COMMITTED;
}

bool checkAbortedState(SessionId xid) {
    std::lock_guard lk(meta_data_mtx);
    auto it = sessions_directory.find(xid);
    return it != sessions_directory.end() && it->second.phase == StateInfo::ROLLED_BACK;
}

struct RecordRevision {
    std::string content;
    SessionId xmin;
    SessionId xmax = 0;
};

static std::mutex storage_heap_mtx;
static std::unordered_map<TupleName, std::list<RecordRevision>> database_heap;

bool verifyVisibilityRules(const RecordRevision& r, SessionId snapshotVal, SessionId readerXid) {
    bool creationVisible = (r.xmin == readerXid) || (checkCommitedState(r.xmin) && r.xmin < snapshotVal);
    if (!creationVisible) return false;
    if (r.xmax == 0) return true;
    bool deletionVisible = (r.xmax == readerXid) || (checkCommitedState(r.xmax) && r.xmax < snapshotVal);
    return !deletionVisible;
}

std::optional<std::string> readTuple(const TupleName& key, SessionId xid) {
    std::lock_guard lk(storage_heap_mtx);
    SessionId snap;
    {
        std::lock_guard tlk(meta_data_mtx);
        snap = sessions_directory.at(xid).viewSnapshot;
    }
    auto it = database_heap.find(key);
    if (it == database_heap.end()) return std::nullopt;
    for (auto& revision : it->second) {
        if (verifyVisibilityRules(revision, snap, xid)) return revision.content;
    }
    return std::nullopt;
}

void insertTuple(const TupleName& key, const std::string& data, SessionId xid) {
    std::lock_guard lk(storage_heap_mtx);
    database_heap[key].push_front({data, xid, 0});
}

void updateTuple(const TupleName& key, const std::string& data, SessionId xid) {
    std::lock_guard lk(storage_heap_mtx);
    SessionId snap;
    {
        std::lock_guard tlk(meta_data_mtx);
        snap = sessions_directory.at(xid).viewSnapshot;
    }
    auto it = database_heap.find(key);
    if (it != database_heap.end()) {
        for (auto& revision : it->second) {
            if (verifyVisibilityRules(revision, snap, xid) && revision.xmax == 0) {
                revision.xmax = xid;
                break;
            }
        }
    }
    database_heap[key].push_front({data, xid, 0});
}

void deleteTuple(const TupleName& key, SessionId xid) {
    std::lock_guard lk(storage_heap_mtx);
    SessionId snap;
    {
        std::lock_guard tlk(meta_data_mtx);
        snap = sessions_directory.at(xid).viewSnapshot;
    }
    auto it = database_heap.find(key);
    if (it == database_heap.end()) return;
    for (auto& revision : it->second) {
        if (verifyVisibilityRules(revision, snap, xid) && revision.xmax == 0) {
            revision.xmax = xid;
            return;
        }
    }
}

enum class LockModeType { L_SHARED, L_EXCLUSIVE };

struct RequestEntry {
    SessionId xid;
    LockModeType mode;
    bool granted = false;
};

struct WaitQueueHeader {
    std::list<RequestEntry> requests;
    std::mutex mtx;
    std::condition_variable cv;
};

static std::mutex sync_tables_mtx;
static std::unordered_map<TupleName, WaitQueueHeader> locks_catalog;
static std::unordered_map<SessionId, std::unordered_set<SessionId>> waits_relations_map;

bool checkForCycles(SessionId start, const std::unordered_map<SessionId, std::unordered_set<SessionId>>& graph) {
    std::unordered_set<SessionId> visited, stack;
    std::function<bool(SessionId)> dfs = [&](SessionId node) -> bool {
        visited.insert(node);
        stack.insert(node);
        auto it = graph.find(node);
        if (it != graph.end()) {
            for (SessionId neighbor : it->second) {
                if (!visited.count(neighbor) && dfs(neighbor)) return true;
                if (stack.count(neighbor)) return true;
            }
        }
        stack.erase(node);
        return false;
    };
    return dfs(start);
}

class DependencyException : public std::runtime_error {
public:
    explicit DependencyException(SessionId xid)
        : std::runtime_error("Circular lock wait detected. Session #" + std::to_string(xid) + " rolled back.") {}
};

void claimAccessLock(const TupleName& key, SessionId xid, LockModeType mode) {
    {
        std::lock_guard lk(meta_data_mtx);
        if (sessions_directory.at(xid).lockShrinking) {
            throw std::runtime_error("Strict 2PL violation: Cannot request new locks in shrinking phase");
        }
    }

    WaitQueueHeader& q = locks_catalog[key];
    std::unique_lock ul(q.mtx);

    for (auto& req : q.requests) {
        if (req.xid == xid && req.granted) {
            if (mode == LockModeType::L_SHARED) return;
            if (req.mode == LockModeType::L_EXCLUSIVE) return;
        }
    }

    q.requests.push_back({xid, mode, false});
    auto& myRequest = q.requests.back();

    while (true) {
        bool blocked = false;
        std::unordered_set<SessionId> blockingSet;
        for (auto& req : q.requests) {
            if (&req == &myRequest) break;
            if (!req.granted) continue;
            if (mode == LockModeType::L_EXCLUSIVE || req.mode == LockModeType::L_EXCLUSIVE) {
                if (req.xid != xid) {
                    blocked = true;
                    blockingSet.insert(req.xid);
                }
            }
        }

        if (!blocked) {
            myRequest.granted = true;
            {
                std::lock_guard lk(sync_tables_mtx);
                waits_relations_map.erase(xid);
            }
            return;
        }

        {
            std::lock_guard lk(sync_tables_mtx);
            waits_relations_map[xid] = blockingSet;
            if (checkForCycles(xid, waits_relations_map)) {
                waits_relations_map.erase(xid);
                q.requests.remove_if([&](const RequestEntry& r){ return r.xid == xid && !r.granted; });
                throw DependencyException(xid);
            }
        }

        q.cv.wait(ul);
    }
}

void freeSessionLocks(SessionId xid) {
    {
        std::lock_guard lk(meta_data_mtx);
        if (sessions_directory.count(xid)) {
            sessions_directory.at(xid).lockShrinking = true;
        }
    }

    for (auto& [key, q] : locks_catalog) {
        std::unique_lock ul(q.mtx);
        q.requests.remove_if([&](const RequestEntry& r){ return r.xid == xid; });
        q.cv.notify_all();
    }

    {
        std::lock_guard lk(sync_tables_mtx);
        waits_relations_map.erase(xid);
    }
}

class SchedulerGate {
public:
    SessionId start() { return initializeTransaction(); }

    std::optional<std::string> get(SessionId xid, const TupleName& key) {
        claimAccessLock(key, xid, LockModeType::L_SHARED);
        return readTuple(key, xid);
    }

    void write(SessionId xid, const TupleName& key, const std::string& value) {
        claimAccessLock(key, xid, LockModeType::L_EXCLUSIVE);
        insertTuple(key, value, xid);
    }

    void change(SessionId xid, const TupleName& key, const std::string& value) {
        claimAccessLock(key, xid, LockModeType::L_EXCLUSIVE);
        updateTuple(key, value, xid);
    }

    void erase(SessionId xid, const TupleName& key) {
        claimAccessLock(key, xid, LockModeType::L_EXCLUSIVE);
        deleteTuple(key, xid);
    }

    void commit(SessionId xid) {
        {
            std::lock_guard lk(meta_data_mtx);
            sessions_directory.at(xid).phase = StateInfo::COMMITTED;
        }
        freeSessionLocks(xid);
        std::cout << "[SCHEDULER] Session " << xid << " COMMITTED\n";
    }

    void rollback(SessionId xid) {
        {
            std::lock_guard lk(storage_heap_mtx);
            for (auto& [key, list] : database_heap) {
                for (auto& revision : list) {
                    if (revision.xmin == xid) revision.xmax = xid;
                    if (revision.xmax == xid) revision.xmax = 0;
                }
            }
        }
        {
            std::lock_guard lk(meta_data_mtx);
            sessions_directory.at(xid).phase = StateInfo::ROLLED_BACK;
        }
        freeSessionLocks(xid);
        std::cout << "[SCHEDULER] Session " << xid << " ABORTED (ROLLED BACK)\n";
    }
};

void show_data(const std::optional<std::string>& val, SessionId xid, const TupleName& key) {
    std::cout << "  [Session " << xid << "] READ " << key << " = "
              << (val ? *val : "<not visible>") << "\n";
}

int main() {
    SchedulerGate engine;

    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        SessionId s1 = engine.start();
        engine.write(s1, "checking_amt", "1000");
        engine.commit(s1);

        SessionId s2 = engine.start();
        SessionId s3 = engine.start();

        engine.change(s3, "checking_amt", "2000");
        engine.commit(s3);

        auto val = engine.get(s2, "checking_amt");
        show_data(val, s2, "checking_amt");
        engine.commit(s2);
    }

    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        SessionId s4 = engine.start();
        SessionId s5 = engine.start();
        show_data(engine.get(s4, "checking_amt"), s4, "checking_amt");
        show_data(engine.get(s5, "checking_amt"), s5, "checking_amt");
        engine.commit(s4);
        engine.commit(s5);
    }

    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    {
        SessionId s6 = engine.start();
        engine.change(s6, "checking_amt", "3000");

        std::thread client_thread([&]() {
            SessionId s7 = engine.start();
            std::cout << "  [Session " << s7 << "] waiting for SHARED lock on checking_amt...\n";
            auto val = engine.get(s7, "checking_amt");
            show_data(val, s7, "checking_amt");
            engine.commit(s7);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        engine.commit(s6);
        client_thread.join();
    }

    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        SessionId sa = engine.start();
        SessionId sb = engine.start();

        engine.write(sa, "item_alpha", "val_x");
        engine.write(sb, "item_beta", "val_y");
        engine.commit(sa);
        engine.commit(sb);

        SessionId s8 = engine.start();
        SessionId s9 = engine.start();

        claimAccessLock("item_alpha", s8, LockModeType::L_EXCLUSIVE);
        claimAccessLock("item_beta", s9, LockModeType::L_EXCLUSIVE);

        std::thread t1([&]() {
            try {
                claimAccessLock("item_beta", s8, LockModeType::L_EXCLUSIVE);
                engine.commit(s8);
            } catch (DependencyException& e) {
                std::cout << "  " << e.what() << "\n";
                engine.rollback(s8);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        try {
            claimAccessLock("item_alpha", s9, LockModeType::L_EXCLUSIVE);
            engine.commit(s9);
        } catch (DependencyException& e) {
            std::cout << "  " << e.what() << "\n";
            engine.rollback(s9);
        }

        t1.join();
    }

    std::cout << "\nAll scenarios complete.\n";
    return 0;
}
