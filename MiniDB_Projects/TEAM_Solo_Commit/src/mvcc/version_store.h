// MiniDB - Track B extension: Multi-Version Concurrency Control.
//
// Instead of locking a row for the whole transaction (Strict 2PL), every write creates a NEW
// version of the row and leaves the old ones in place. Each version records the commit
// timestamp at which it became visible and the timestamp at which it was superseded, so a
// reader simply picks the version that was current as of its own snapshot. Readers therefore
// never block writers and writers never block readers: the central property this store exists
// to demonstrate. It is the version-chain idea from Lab 6, made timestamp-ordered with proper
// snapshot visibility.
//
// Versions form a newest-first singly linked chain per key:
//     head -> v3(begin=30) -> v2(begin=20) -> v1(begin=10) -> null
// A reader at snapshot S returns the newest committed version with begin_ts <= S (unless that
// version is a delete). Uncommitted and future-committed versions are invisible.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

class VersionStore {
public:
    static constexpr int64_t PENDING = -1;             // begin_ts before commit
    static constexpr int64_t INF = INT64_MAX;          // end_ts while still current

    ~VersionStore();

    // Append an uncommitted version created by `txn`. `deleted` marks a tombstone version.
    void Write(int txn, int64_t key, const std::string& value, bool deleted = false);

    // Read the version visible at `snapshot_ts`. Returns false if no visible version (or it is
    // a delete). Never blocks on a concurrent writer.
    bool ReadSnapshot(int64_t snapshot_ts, int64_t key, std::string* out) const;

    // Stamp all of `txn`'s pending versions as committed at `commit_ts`.
    void Commit(int txn, int64_t commit_ts);
    // Discard `txn`'s pending versions (mark them never-visible).
    void Abort(int txn);

    size_t VersionCount(int64_t key) const;  // diagnostics

private:
    struct Version {
        int64_t begin_ts;   // commit timestamp, or PENDING
        int64_t end_ts;     // commit ts of the version that superseded this one, or INF
        int txn;
        bool committed;
        bool aborted;
        bool deleted;
        std::string value;
        Version* older;
    };

    mutable std::mutex mu_;
    std::unordered_map<int64_t, Version*> heads_;            // key -> newest version
    std::unordered_map<int, std::vector<Version*>> pending_; // txn -> its uncommitted versions
};

}  // namespace minidb
