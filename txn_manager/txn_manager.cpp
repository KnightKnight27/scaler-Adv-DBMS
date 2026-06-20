#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using TxId = std::uint64_t;
using Key = std::string;

enum class TxState { Active, Committed, Aborted };

struct TxRecord {
    TxId id = 0;
    TxId snapshot = 0;
    TxState state = TxState::Active;
    bool shrinking = false;
};

class TxDirectory {
public:
    TxId begin() {
        std::lock_guard<std::mutex> guard(mu_);
        const TxId id = next_++;
        tx_[id] = TxRecord{id, id, TxState::Active, false};
        return id;
    }

    TxId snapshot(TxId id) {
        std::lock_guard<std::mutex> guard(mu_);
        return tx_.at(id).snapshot;
    }

    bool committed(TxId id) {
        std::lock_guard<std::mutex> guard(mu_);
        auto it = tx_.find(id);
        return it != tx_.end() && it->second.state == TxState::Committed;
    }

    void commit(TxId id) {
        std::lock_guard<std::mutex> guard(mu_);
        tx_.at(id).state = TxState::Committed;
    }

    void abort(TxId id) {
        std::lock_guard<std::mutex> guard(mu_);
        tx_.at(id).state = TxState::Aborted;
    }

    void enter_shrinking(TxId id) {
        std::lock_guard<std::mutex> guard(mu_);
        tx_.at(id).shrinking = true;
    }

    void assert_growing(TxId id) {
        std::lock_guard<std::mutex> guard(mu_);
        if (tx_.at(id).shrinking) {
            throw std::runtime_error("2PL violation: transaction already released locks");
        }
    }

private:
    std::mutex mu_;
    TxId next_ = 1;
    std::unordered_map<TxId, TxRecord> tx_;
};

TxDirectory g_tx;

struct Version {
    std::string value;
    TxId xmin = 0;
    TxId xmax = 0;
};

class VersionStore {
public:
    std::optional<std::string> read(const Key& key, TxId reader) {
        std::lock_guard<std::mutex> guard(mu_);
        const TxId snapshot = g_tx.snapshot(reader);
        auto it = rows_.find(key);
        if (it == rows_.end()) {
            return std::nullopt;
        }

        for (const Version& version : it->second) {
            if (visible(version, snapshot, reader)) {
                return version.value;
            }
        }
        return std::nullopt;
    }

    void insert(const Key& key, std::string value, TxId writer) {
        std::lock_guard<std::mutex> guard(mu_);
        rows_[key].push_front(Version{std::move(value), writer, 0});
    }

    void update(const Key& key, std::string value, TxId writer) {
        std::lock_guard<std::mutex> guard(mu_);
        const TxId snapshot = g_tx.snapshot(writer);
        for (Version& version : rows_[key]) {
            if (visible(version, snapshot, writer) && version.xmax == 0) {
                version.xmax = writer;
                break;
            }
        }
        rows_[key].push_front(Version{std::move(value), writer, 0});
    }

    void remove(const Key& key, TxId writer) {
        std::lock_guard<std::mutex> guard(mu_);
        const TxId snapshot = g_tx.snapshot(writer);
        auto it = rows_.find(key);
        if (it == rows_.end()) {
            return;
        }
        for (Version& version : it->second) {
            if (visible(version, snapshot, writer) && version.xmax == 0) {
                version.xmax = writer;
                return;
            }
        }
    }

    void rollback(TxId id) {
        std::lock_guard<std::mutex> guard(mu_);
        for (auto& pair : rows_) {
            for (Version& version : pair.second) {
                if (version.xmin == id) {
                    version.xmax = id;
                } else if (version.xmax == id) {
                    version.xmax = 0;
                }
            }
        }
    }

private:
    std::mutex mu_;
    std::unordered_map<Key, std::list<Version>> rows_;

    bool visible(const Version& version, TxId snapshot, TxId reader) {
        const bool creator_visible =
            version.xmin == reader || (g_tx.committed(version.xmin) && version.xmin < snapshot);
        if (!creator_visible) {
            return false;
        }
        if (version.xmax == 0) {
            return true;
        }
        const bool deleted_before_snapshot =
            version.xmax == reader || (g_tx.committed(version.xmax) && version.xmax < snapshot);
        return !deleted_before_snapshot;
    }
};

VersionStore g_versions;

enum class LockMode { Shared, Exclusive };

struct Request {
    TxId tx = 0;
    LockMode mode = LockMode::Shared;
    bool granted = false;
};

struct LockQueue {
    std::list<Request> requests;
    std::mutex mu;
    std::condition_variable cv;
};

class Deadlock : public std::runtime_error {
public:
    explicit Deadlock(TxId id) : std::runtime_error("deadlock detected for tx " + std::to_string(id)) {}
};

class LockTable {
public:
    void acquire(const Key& key, TxId tx, LockMode mode) {
        g_tx.assert_growing(tx);
        LockQueue& queue = table_[key];
        std::unique_lock<std::mutex> lock(queue.mu);

        for (Request& request : queue.requests) {
            if (request.tx == tx && request.granted) {
                if (request.mode == LockMode::Exclusive || mode == LockMode::Shared) {
                    return;
                }
            }
        }

        queue.requests.push_back(Request{tx, mode, false});
        auto mine = std::prev(queue.requests.end());

        while (true) {
            std::unordered_set<TxId> blockers;
            for (auto it = queue.requests.begin(); it != mine; ++it) {
                if (!it->granted || it->tx == tx) {
                    continue;
                }
                if (mode == LockMode::Exclusive || it->mode == LockMode::Exclusive) {
                    blockers.insert(it->tx);
                }
            }

            if (blockers.empty()) {
                mine->granted = true;
                clear_wait(tx);
                return;
            }

            set_wait(tx, blockers);
            if (cycle_from(tx)) {
                queue.requests.erase(mine);
                clear_wait(tx);
                queue.cv.notify_all();
                throw Deadlock(tx);
            }

            queue.cv.wait(lock);
        }
    }

    void release_all(TxId tx) {
        g_tx.enter_shrinking(tx);
        for (auto& pair : table_) {
            LockQueue& queue = pair.second;
            std::unique_lock<std::mutex> lock(queue.mu);
            queue.requests.remove_if([&](const Request& request) {
                return request.tx == tx;
            });
            queue.cv.notify_all();
        }
        clear_wait(tx);
    }

private:
    std::unordered_map<Key, LockQueue> table_;
    std::mutex graph_mu_;
    std::unordered_map<TxId, std::unordered_set<TxId>> waits_for_;

    void set_wait(TxId tx, const std::unordered_set<TxId>& blockers) {
        std::lock_guard<std::mutex> guard(graph_mu_);
        waits_for_[tx] = blockers;
    }

    void clear_wait(TxId tx) {
        std::lock_guard<std::mutex> guard(graph_mu_);
        waits_for_.erase(tx);
        for (auto& pair : waits_for_) {
            pair.second.erase(tx);
        }
    }

    bool cycle_from(TxId start) {
        std::lock_guard<std::mutex> guard(graph_mu_);
        std::unordered_set<TxId> visiting;
        std::unordered_set<TxId> done;

        std::function<bool(TxId)> dfs = [&](TxId node) {
            if (visiting.count(node)) {
                return true;
            }
            if (done.count(node)) {
                return false;
            }
            visiting.insert(node);
            for (TxId next : waits_for_[node]) {
                if (dfs(next)) {
                    return true;
                }
            }
            visiting.erase(node);
            done.insert(node);
            return false;
        };

        return dfs(start);
    }
};

LockTable g_locks;

class TransactionManager {
public:
    TxId begin() {
        return g_tx.begin();
    }

    std::optional<std::string> read(TxId tx, const Key& key) {
        g_locks.acquire(key, tx, LockMode::Shared);
        return g_versions.read(key, tx);
    }

    void insert(TxId tx, const Key& key, const std::string& value) {
        g_locks.acquire(key, tx, LockMode::Exclusive);
        g_versions.insert(key, value, tx);
    }

    void update(TxId tx, const Key& key, const std::string& value) {
        g_locks.acquire(key, tx, LockMode::Exclusive);
        g_versions.update(key, value, tx);
    }

    void remove(TxId tx, const Key& key) {
        g_locks.acquire(key, tx, LockMode::Exclusive);
        g_versions.remove(key, tx);
    }

    void commit(TxId tx) {
        g_tx.commit(tx);
        g_locks.release_all(tx);
        std::cout << "tx " << tx << " commit\n";
    }

    void abort(TxId tx) {
        g_versions.rollback(tx);
        g_tx.abort(tx);
        g_locks.release_all(tx);
        std::cout << "tx " << tx << " abort\n";
    }
};

void show(TxId tx, const Key& key, const std::optional<std::string>& value) {
    std::cout << "tx " << tx << " read " << key << " = "
              << (value ? *value : "<none>") << '\n';
}

int main() {
    TransactionManager tm;

    std::cout << "snapshot demo\n";
    TxId seed = tm.begin();
    tm.insert(seed, "account", "100");
    tm.commit(seed);

    TxId old_reader = tm.begin();
    TxId writer = tm.begin();
    tm.update(writer, "account", "175");
    tm.commit(writer);
    show(old_reader, "account", tm.read(old_reader, "account"));
    tm.commit(old_reader);

    std::cout << "\nshared lock demo\n";
    TxId r1 = tm.begin();
    TxId r2 = tm.begin();
    show(r1, "account", tm.read(r1, "account"));
    show(r2, "account", tm.read(r2, "account"));
    tm.commit(r1);
    tm.commit(r2);

    std::cout << "\nwaiting demo\n";
    TxId holder = tm.begin();
    tm.update(holder, "account", "250");
    std::thread blocked_reader([&] {
        TxId tx = tm.begin();
        std::cout << "tx " << tx << " waits for account\n";
        show(tx, "account", tm.read(tx, "account"));
        tm.commit(tx);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    tm.commit(holder);
    blocked_reader.join();

    std::cout << "\ndeadlock demo\n";
    TxId a = tm.begin();
    TxId b = tm.begin();
    tm.insert(a, "A", "left");
    tm.insert(b, "B", "right");
    tm.commit(a);
    tm.commit(b);

    TxId t1 = tm.begin();
    TxId t2 = tm.begin();
    g_locks.acquire("A", t1, LockMode::Exclusive);
    g_locks.acquire("B", t2, LockMode::Exclusive);

    std::thread left([&] {
        try {
            g_locks.acquire("B", t1, LockMode::Exclusive);
            tm.commit(t1);
        } catch (const Deadlock& error) {
            std::cout << error.what() << '\n';
            tm.abort(t1);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    try {
        g_locks.acquire("A", t2, LockMode::Exclusive);
        tm.commit(t2);
    } catch (const Deadlock& error) {
        std::cout << error.what() << '\n';
        tm.abort(t2);
    }
    left.join();

    return 0;
}
