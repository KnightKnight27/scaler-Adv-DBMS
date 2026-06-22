#pragma once
#include <climits>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "common/config.h"

namespace minidb {

// One version of a row in a version chain.
struct Version {
  long    begin_ts;   // commit timestamp; kInf while uncommitted
  long    end_ts;     // kInf = still the current version
  int32_t value;
  TxnId   creator;
  bool    committed;
};

// A multi-version store implementing snapshot isolation (Track B extension).
// Each key owns a chain of versions. A reader sees the version that was
// committed as of its snapshot timestamp, so reads NEVER take locks or block --
// the core advantage demonstrated against 2PL. Writers create a new
// uncommitted version; conflicting concurrent writers are rejected
// (first-committer-wins), preventing lost updates.
class MvccStore {
 public:
  static const long kInf = LONG_MAX;

  // Seed a key with an initial committed value (begin_ts = 0).
  void Init(int64_t key, int32_t value);

  // Read the value visible at `snapshot`. Returns false if the key is unknown.
  bool Read(int64_t key, long snapshot, int32_t* out) const;

  // Create a new uncommitted version. Returns false on a write-write conflict.
  bool Write(int64_t key, TxnId txn, long snapshot, int32_t value);

  // Commit txn's pending versions at `commit_ts`, closing the prior version.
  void Commit(TxnId txn, long commit_ts);

  // Discard txn's uncommitted versions.
  void Abort(TxnId txn);

  // Number of versions retained for a key (illustrates version growth / GC).
  size_t VersionCount(int64_t key) const;

 private:
  std::unordered_map<int64_t, std::vector<Version>> chains_;
};

}  // namespace minidb
