// txn_manager.h
// -----------------------------------------------------------------------------
// Transaction manager: ties MVCC version chains (version_store) together with
// Strict 2PL + deadlock detection (lock_manager) and exposes the txn API.
//
// CONCURRENCY MODEL: MV2PL (multi-version 2PL).
//   * READS  -> lock-free MVCC snapshot reads (no S locks taken).
//   * WRITES -> exclusive (X) locks under Strict 2PL, held until commit/abort.
// See README.md for the full rationale and visibility rule.
//
// Timestamps: a single monotonically increasing counter (`clock_`) issues both
// begin-ts (snapshot taken at begin()) and commit-ts (taken at commit()).
// -----------------------------------------------------------------------------
#ifndef LAB8_TXN_MANAGER_H
#define LAB8_TXN_MANAGER_H

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "lock_manager.h"
#include "version_store.h"

namespace lab8 {

enum class TxnState { Active, Committed, Aborted };

struct Txn {
    TxnId    id        = 0;
    Ts       begin_ts  = 0;        // snapshot timestamp (taken at begin)
    Ts       commit_ts = INF;      // assigned at commit
    TxnState state      = TxnState::Active;

    // Set of commit-ts values that were already committed when this txn began.
    // A committed version is visible to this txn iff its begin_ts is in this
    // set OR <= our begin_ts. We use the begin_ts watermark + the snapshot set
    // for clarity; with a single serial commit counter the watermark suffices,
    // but we keep the set to make the rule explicit and robust.
    Ts snapshot = 0;               // = begin_ts; committed-before watermark

    // Bookkeeping for rollback: keys this txn created an uncommitted version on.
    std::vector<std::string>            written_keys;
    // The uncommitted version node we created per key (for commit/rollback).
    std::unordered_map<std::string, VersionPtr> my_versions;
};

// Outcome of read/write so the deterministic simulator can react.
enum class OpStatus { Ok, Blocked, Aborted };

struct ReadResult {
    OpStatus              status;
    std::optional<Value>  value;   // present (and may be empty-key) only if Ok
    bool                  found = false;  // key visible & not a tombstone
};

class TxnManager {
public:
    TxnId begin();

    // Snapshot read. Never blocks (MVCC). `found` is false if the key is not
    // visible to the snapshot or the visible version is a tombstone.
    ReadResult read(TxnId txn, const std::string& key);

    // Write under Strict 2PL: takes X lock (held to end). Returns:
    //   Ok      -> new version created
    //   Blocked -> lock held by another active txn (no deadlock)
    //   Aborted -> a deadlock was detected and THIS txn was chosen as victim
    OpStatus write(TxnId txn, const std::string& key, Value v);

    // Delete = write a tombstone version (same locking rules as write).
    OpStatus remove(TxnId txn, const std::string& key);

    void commit(TxnId txn);
    void abort(TxnId txn);

    // --- introspection for the demo/self-tests ---
    TxnState state(TxnId txn) const;
    const LockManager& locks() const { return lm_; }
    LockManager& locks() { return lm_; }

private:
    // Core: walk key's version chain and return the version visible to `t`.
    VersionPtr visible_version(const Txn& t, const std::string& key) const;

    OpStatus do_write(TxnId txn, const std::string& key, Value v,
                      bool tombstone);

    Ts            clock_ = 0;            // monotonic ts/id source
    VersionStore  store_;
    LockManager   lm_;
    std::unordered_map<TxnId, Txn> txns_;
};

}  // namespace lab8

#endif  // LAB8_TXN_MANAGER_H
