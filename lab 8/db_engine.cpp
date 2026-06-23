#include "db_engine.hpp"

#include <iostream>
#include <stdexcept>

using std::cout;
using std::runtime_error;
using std::string;

// ---- VersionStore ----------------------------------------------------------

void VersionStore::bootstrap(const string& key, int value) {
    table_[key].push_back({value, 0, ++logical_clock_});
}

int VersionStore::current_time() const {
    return logical_clock_;
}

int VersionStore::read_visible(const string& key, const TxContext& tx) const {
    const auto& chain = table_.at(key);

    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (it->commit_time <= tx.snap_time)
            return it->value;
    }

    throw runtime_error("no committed version visible at snapshot");
}

void VersionStore::apply_commit(TxContext& tx) {
    int ts = ++logical_clock_;

    for (const auto& [key, val] : tx.staged_writes)
        table_[key].push_back({val, tx.id, ts});

    tx.status = TxStatus::Committed;
}

void VersionStore::dump_chains() const {
    cout << "\n--- version chains ---\n";

    for (const auto& [key, chain] : table_) {
        cout << key << ": ";
        for (const RowVersion& v : chain) {
            cout << "(v=" << v.value << ", tx=" << v.author_tx
                 << ", ts=" << v.commit_time << ") ";
        }
        cout << '\n';
    }
}

// ---- TwoPhaseLockTable -----------------------------------------------------

bool TwoPhaseLockTable::request(TxContext& tx, const string& key, LockType type) {
    if (tx.past_growing_phase) {
        cout << "T" << tx.id << " violates 2PL: lock requested in shrinking phase\n";
        tx.status = TxStatus::Aborted;
        return false;
    }

    ResourceLock& res = resources_[key];
    std::set<int> blockers = find_blockers(tx.id, res, type);

    if (blockers.empty()) {
        grant(tx, key, res, type);
        scrub_wait_edges(tx.id);
        tx.status = TxStatus::Running;
        cout << "T" << tx.id << " got "
             << (type == LockType::Shared ? "S" : "X") << "-lock on " << key << '\n';
        return true;
    }

    wait_edges_[tx.id] = blockers;
    tx.status = TxStatus::Waiting;

    cout << "T" << tx.id << " blocked on " << key << " by ";
    for (int b : blockers) cout << "T" << b << ' ';
    cout << '\n';

    if (graph_has_cycle()) {
        cout << "cycle in wait-for graph → abort T" << tx.id << '\n';
        force_abort(tx);
    }

    return false;
}

void TwoPhaseLockTable::drop_all(TxContext& tx) {
    tx.past_growing_phase = true;

    for (const string& key : tx.shared_keys)
        resources_[key].shared_holders.erase(tx.id);

    for (const string& key : tx.exclusive_keys) {
        if (resources_[key].exclusive_holder == tx.id)
            resources_[key].exclusive_holder = -1;
    }

    tx.shared_keys.clear();
    tx.exclusive_keys.clear();
    scrub_wait_edges(tx.id);
}

void TwoPhaseLockTable::force_abort(TxContext& tx) {
    tx.staged_writes.clear();
    tx.status = TxStatus::Aborted;
    drop_all(tx);
}

void TwoPhaseLockTable::dump_waits() const {
    cout << "\n--- wait-for graph ---\n";

    if (wait_edges_.empty()) {
        cout << "(empty)\n";
        return;
    }

    for (const auto& [waiter, blockers] : wait_edges_) {
        cout << "T" << waiter << " → ";
        for (int b : blockers) cout << "T" << b << ' ';
        cout << '\n';
    }
}

std::set<int> TwoPhaseLockTable::find_blockers(int tx_id, const ResourceLock& res,
                                              LockType type) const {
    std::set<int> blockers;

    if (res.exclusive_holder != -1 && res.exclusive_holder != tx_id)
        blockers.insert(res.exclusive_holder);

    if (type == LockType::Exclusive) {
        for (int holder : res.shared_holders) {
            if (holder != tx_id) blockers.insert(holder);
        }
    }

    return blockers;
}

void TwoPhaseLockTable::grant(TxContext& tx, const string& key, ResourceLock& res,
                              LockType type) {
    if (type == LockType::Shared) {
        res.shared_holders.insert(tx.id);
        tx.shared_keys.insert(key);
        return;
    }

    res.shared_holders.erase(tx.id);
    tx.shared_keys.erase(key);
    res.exclusive_holder = tx.id;
    tx.exclusive_keys.insert(key);
}

bool TwoPhaseLockTable::graph_has_cycle() const {
    std::set<int> seen, path;

    for (const auto& [node, _] : wait_edges_) {
        if (dfs_cycle(node, seen, path)) return true;
    }
    return false;
}

bool TwoPhaseLockTable::dfs_cycle(int node, std::set<int>& seen,
                                  std::set<int>& path) const {
    if (path.count(node)) return true;
    if (seen.count(node)) return false;

    seen.insert(node);
    path.insert(node);

    auto it = wait_edges_.find(node);
    if (it != wait_edges_.end()) {
        for (int next : it->second) {
            if (dfs_cycle(next, seen, path)) return true;
        }
    }

    path.erase(node);
    return false;
}

void TwoPhaseLockTable::scrub_wait_edges(int tx_id) {
    wait_edges_.erase(tx_id);

    for (auto& [_, blockers] : wait_edges_)
        blockers.erase(tx_id);

    for (auto it = wait_edges_.begin(); it != wait_edges_.end();) {
        if (it->second.empty()) it = wait_edges_.erase(it);
        else ++it;
    }
}

// ---- DbEngine --------------------------------------------------------------

DbEngine::DbEngine() {
    store_.bootstrap("A", 100);
    store_.bootstrap("B", 200);
}

TxContext DbEngine::open_txn() {
    TxContext tx;
    tx.id = next_id_++;
    tx.snap_time = store_.current_time();
    cout << "\nopen T" << tx.id << " (snapshot ts=" << tx.snap_time << ")\n";
    return tx;
}

void DbEngine::read(TxContext& tx, const string& key) {
    if (tx.status == TxStatus::Aborted) return;

    if (locks_.request(tx, key, LockType::Shared)) {
        int val = store_.read_visible(key, tx);
        cout << "T" << tx.id << " read " << key << " = " << val << '\n';
    }
}

void DbEngine::write(TxContext& tx, const string& key, int value) {
    if (tx.status == TxStatus::Aborted) return;

    if (locks_.request(tx, key, LockType::Exclusive)) {
        tx.staged_writes.push_back({key, value});
        cout << "T" << tx.id << " staged " << key << " = " << value << '\n';
    }
}

void DbEngine::commit(TxContext& tx) {
    if (tx.status == TxStatus::Aborted) {
        cout << "T" << tx.id << " already aborted — skip commit\n";
        return;
    }

    store_.apply_commit(tx);
    locks_.drop_all(tx);
    cout << "T" << tx.id << " committed\n";
}

void DbEngine::print_state() const {
    locks_.dump_waits();
    store_.dump_chains();
}
