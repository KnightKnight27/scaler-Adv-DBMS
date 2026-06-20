#include <algorithm>
#include <cstdint>
#include <iostream>
#include <list>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

using TxId = std::uint64_t;
using Timestamp = std::uint64_t;
using Key = std::string;

enum class TxStatus { Active, Committed, Aborted };

class Deadlock : public std::runtime_error {
public:
    explicit Deadlock(TxId tx) : std::runtime_error("deadlock detected for tx " + std::to_string(tx)) {}
};

class SerializationFailure : public std::runtime_error {
public:
    explicit SerializationFailure(TxId tx)
        : std::runtime_error("serialization failure for tx " + std::to_string(tx)) {}
};

struct Transaction {
    TxId id = 0;
    Timestamp snapshot = 0;
    TxStatus status = TxStatus::Active;
    std::unordered_set<Key> write_set;
};

class LockManager {
public:
    bool acquire_exclusive(const Key& key, TxId tx) {
        auto owner = owners_.find(key);
        if (owner == owners_.end()) {
            owners_[key] = tx;
            clear_wait(tx);
            std::cout << "tx " << tx << " X-lock " << key << '\n';
            return true;
        }

        if (owner->second == tx) {
            return true;
        }

        waits_for_[tx].insert(owner->second);
        std::cout << "tx " << tx << " waits for tx " << owner->second
                  << " on " << key << '\n';

        if (has_cycle_from(tx)) {
            waits_for_[tx].erase(owner->second);
            throw Deadlock(tx);
        }
        return false;
    }

    void release_all(TxId tx) {
        for (auto it = owners_.begin(); it != owners_.end();) {
            if (it->second == tx) {
                std::cout << "tx " << tx << " unlock " << it->first << '\n';
                it = owners_.erase(it);
            } else {
                ++it;
            }
        }
        clear_wait(tx);
    }

private:
    std::unordered_map<Key, TxId> owners_;
    std::unordered_map<TxId, std::unordered_set<TxId>> waits_for_;

    void clear_wait(TxId tx) {
        waits_for_.erase(tx);
        for (auto& entry : waits_for_) {
            entry.second.erase(tx);
        }
    }

    bool has_cycle_from(TxId start) const {
        std::unordered_set<TxId> visiting;
        std::unordered_set<TxId> done;

        auto dfs = [&](auto&& self, TxId node) -> bool {
            if (visiting.count(node)) {
                return true;
            }
            if (done.count(node)) {
                return false;
            }

            visiting.insert(node);
            auto it = waits_for_.find(node);
            if (it != waits_for_.end()) {
                for (TxId next : it->second) {
                    if (self(self, next)) {
                        return true;
                    }
                }
            }
            visiting.erase(node);
            done.insert(node);
            return false;
        };

        return dfs(dfs, start);
    }
};

struct Version {
    std::string value;
    TxId creator = 0;
    Timestamp created_at = 0;
    TxId deleter = 0;
    Timestamp deleted_at = 0;
    bool tombstone = false;
};

class TransactionManager {
public:
    TxId begin() {
        const TxId id = next_tx_++;
        transactions_[id] = Transaction{id, clock_, TxStatus::Active, {}};
        std::cout << "tx " << id << " begin snapshot=" << clock_ << '\n';
        return id;
    }

    std::optional<std::string> read(TxId tx, const Key& key) {
        assert_active(tx);
        auto found = versions_.find(key);
        if (found == versions_.end()) {
            return std::nullopt;
        }

        for (const Version& version : found->second) {
            if (visible(version, tx)) {
                if (version.tombstone) {
                    return std::nullopt;
                }
                return version.value;
            }
        }
        return std::nullopt;
    }

    bool put(TxId tx, const Key& key, std::string value) {
        assert_active(tx);
        if (!locks_.acquire_exclusive(key, tx)) {
            return false;
        }

        Transaction& record = transactions_.at(tx);
        record.write_set.insert(key);
        mark_visible_version_deleted(tx, key);
        versions_[key].push_front(Version{std::move(value), tx, 0, 0, 0, false});
        std::cout << "tx " << tx << " write " << key << '\n';
        return true;
    }

    bool erase(TxId tx, const Key& key) {
        assert_active(tx);
        if (!locks_.acquire_exclusive(key, tx)) {
            return false;
        }

        Transaction& record = transactions_.at(tx);
        record.write_set.insert(key);
        mark_visible_version_deleted(tx, key);
        versions_[key].push_front(Version{"", tx, 0, 0, 0, true});
        std::cout << "tx " << tx << " delete " << key << '\n';
        return true;
    }

    void commit(TxId tx) {
        assert_active(tx);
        Transaction& record = transactions_.at(tx);

        if (has_write_write_conflict(record)) {
            abort(tx);
            throw SerializationFailure(tx);
        }

        const Timestamp commit_ts = ++clock_;
        for (auto& row : versions_) {
            for (Version& version : row.second) {
                if (version.creator == tx) {
                    version.created_at = commit_ts;
                }
                if (version.deleter == tx) {
                    version.deleted_at = commit_ts;
                }
            }
        }

        record.status = TxStatus::Committed;
        locks_.release_all(tx);
        std::cout << "tx " << tx << " commit ts=" << commit_ts << '\n';
    }

    void abort(TxId tx) {
        auto found = transactions_.find(tx);
        if (found == transactions_.end() || found->second.status != TxStatus::Active) {
            return;
        }

        for (auto& row : versions_) {
            auto& chain = row.second;
            for (auto it = chain.begin(); it != chain.end();) {
                if (it->creator == tx) {
                    it = chain.erase(it);
                    continue;
                }
                if (it->deleter == tx) {
                    it->deleter = 0;
                    it->deleted_at = 0;
                }
                ++it;
            }
        }

        found->second.status = TxStatus::Aborted;
        locks_.release_all(tx);
        std::cout << "tx " << tx << " abort\n";
    }

    void dump_versions(const Key& key) const {
        std::cout << key << " versions:";
        auto found = versions_.find(key);
        if (found == versions_.end()) {
            std::cout << " <empty>\n";
            return;
        }

        for (const Version& version : found->second) {
            std::cout << " [";
            if (version.tombstone) {
                std::cout << "<deleted>";
            } else {
                std::cout << version.value;
            }
            std::cout << " xmin=T" << version.creator << '@' << version.created_at;
            if (version.deleter != 0) {
                std::cout << " xmax=T" << version.deleter << '@' << version.deleted_at;
            }
            std::cout << ']';
        }
        std::cout << '\n';
    }

private:
    TxId next_tx_ = 1;
    Timestamp clock_ = 0;
    std::unordered_map<TxId, Transaction> transactions_;
    std::unordered_map<Key, std::list<Version>> versions_;
    LockManager locks_;

    void assert_active(TxId tx) const {
        auto found = transactions_.find(tx);
        if (found == transactions_.end() || found->second.status != TxStatus::Active) {
            throw std::runtime_error("transaction is not active: " + std::to_string(tx));
        }
    }

    bool visible(const Version& version, TxId tx) const {
        const Transaction& record = transactions_.at(tx);
        const bool created_visible =
            version.creator == tx ||
            (version.created_at != 0 && version.created_at <= record.snapshot);
        if (!created_visible) {
            return false;
        }

        if (version.deleter == tx) {
            return false;
        }
        return version.deleted_at == 0 || version.deleted_at > record.snapshot;
    }

    void mark_visible_version_deleted(TxId tx, const Key& key) {
        auto found = versions_.find(key);
        if (found == versions_.end()) {
            return;
        }

        for (Version& version : found->second) {
            if (visible(version, tx) && version.deleter == 0) {
                version.deleter = tx;
                return;
            }
        }
    }

    bool has_write_write_conflict(const Transaction& record) const {
        for (const Key& key : record.write_set) {
            auto found = versions_.find(key);
            if (found == versions_.end()) {
                continue;
            }
            for (const Version& version : found->second) {
                if (version.creator != record.id && version.created_at > record.snapshot) {
                    return true;
                }
            }
        }
        return false;
    }
};

void show_read(const std::string& label, const std::optional<std::string>& value) {
    std::cout << label << ": " << (value ? *value : "<none>") << '\n';
}

int main() {
    TransactionManager tm;

    std::cout << "snapshot demo\n";
    TxId seed = tm.begin();
    tm.put(seed, "account", "100");
    tm.commit(seed);

    TxId old_reader = tm.begin();
    TxId writer = tm.begin();
    tm.put(writer, "account", "175");
    tm.commit(writer);
    show_read("old reader sees", tm.read(old_reader, "account"));
    tm.commit(old_reader);

    TxId fresh_reader = tm.begin();
    show_read("fresh reader sees", tm.read(fresh_reader, "account"));
    tm.commit(fresh_reader);
    tm.dump_versions("account");

    std::cout << "\nfirst-updater-wins demo\n";
    TxId slow = tm.begin();
    TxId fast = tm.begin();
    tm.put(fast, "account", "220");
    tm.commit(fast);
    tm.put(slow, "account", "190");
    try {
        tm.commit(slow);
    } catch (const SerializationFailure& error) {
        std::cout << error.what() << '\n';
    }
    tm.dump_versions("account");

    std::cout << "\ndeadlock demo\n";
    TxId left = tm.begin();
    TxId right = tm.begin();
    tm.put(left, "left-key", "L1");
    tm.put(right, "right-key", "R1");

    if (!tm.put(left, "right-key", "L2")) {
        std::cout << "tx " << left << " remains active while waiting\n";
    }

    try {
        tm.put(right, "left-key", "R2");
        tm.commit(right);
    } catch (const Deadlock& error) {
        std::cout << error.what() << '\n';
        tm.abort(right);
    }

    tm.put(left, "right-key", "L2");
    tm.commit(left);
    tm.dump_versions("left-key");
    tm.dump_versions("right-key");

    return 0;
}
