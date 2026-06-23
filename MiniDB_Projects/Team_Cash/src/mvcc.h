// Extension - Track B: Multi-Version Concurrency Control (MVCC).
//
// Instead of locking a row for reads, every key keeps a chain of versions.
// Each version records the value and the id of the transaction that created
// it. A transaction reads against a snapshot: the commit-id taken when it
// started. It sees the newest version whose creator committed at or before its
// snapshot. Writers add a new version to the head of the chain.
//
// The headline property: readers never block writers and writers never block
// readers, because a reader simply looks further down the version chain for a
// version it is allowed to see. We compare this against 2PL in the benchmark.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace minidb {

class MVCCStore {
public:
    // A write by `txn`, becoming visible once `txn` is marked committed.
    void put(int key, const std::string& value, int txn);
    void commit(int txn) { committed_[txn] = true; }

    // Read the value visible to a transaction whose snapshot is `snapshot`
    // (it sees writes from committed transactions with id <= snapshot).
    bool read(int key, int snapshot, std::string& out) const;

    int versionCount(int key) const;

private:
    struct Version { std::string value; int txn; };
    std::map<int, std::vector<Version>> chains_;  // key -> versions, newest last
    std::map<int, bool> committed_;
};

}  // namespace minidb
