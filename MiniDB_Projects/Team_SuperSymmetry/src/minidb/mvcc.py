"""
Multi-Version Concurrency Control store (Extension Track B).

This is the heart of the MVCC extension. It keeps, for every logical row, a
*chain of versions* instead of a single value. Readers never block: a
transaction reads the snapshot of the database as of its start timestamp.
Writers create new versions and never overwrite committed data in place.

Design (textbook snapshot isolation with first-committer-wins):

  * A global monotonic timestamp counter hands out start- and commit-stamps.
  * Each row (identified by a synthetic integer rid) owns a list of Versions
    ordered oldest -> newest.
  * A Version has:
        data      - serialized tuple bytes, or None for a delete tombstone
        begin_ts  - commit timestamp at which it became visible
                    (a large provisional value while uncommitted)
        end_ts    - timestamp at which it was superseded (None = still live)
        creator   - txn_id that produced it
        committed - whether begin_ts is final
  * Visibility for a transaction T with snapshot read_ts:
        a version is visible iff
            (it is committed and begin_ts <= read_ts and
             (end_ts is None or end_ts > read_ts))
            OR it was created by T itself and not yet superseded by T.
  * Writes append an uncommitted version. A write-write conflict
    (someone committed a newer version after our snapshot, or another active
    txn holds an uncommitted version on the row) raises MVCCConflict, so the
    caller aborts -> first committer wins.
  * commit() stamps every version the txn created with the commit timestamp
    and closes the version it superseded; abort() discards them.

Readers take no locks at all, which is exactly the property the benchmark
exercises against strict 2PL.
"""
from __future__ import annotations

import threading
from typing import Any, Dict, Iterator, List, Optional, Tuple

UNCOMMITTED = 1 << 62  # provisional begin_ts sentinel for in-flight versions


class MVCCConflict(Exception):
    """Raised on a write-write conflict; the caller must abort."""


class Version:
    __slots__ = ("data", "begin_ts", "end_ts", "creator", "committed")

    def __init__(self, data: Optional[bytes], creator: int):
        self.data = data              # None => tombstone (deleted)
        self.begin_ts = UNCOMMITTED   # provisional until commit
        self.end_ts: Optional[int] = None
        self.creator = creator
        self.committed = False

    def is_deleted(self) -> bool:
        return self.data is None


class Clock:
    """A shared monotonic timestamp source. One instance is shared by every
    per-table MVCC store so a transaction's snapshot is consistent across the
    whole database."""

    def __init__(self):
        self._ts = 0
        self._lock = threading.Lock()

    def tick(self) -> int:
        with self._lock:
            self._ts += 1
            return self._ts

    def now(self) -> int:
        with self._lock:
            return self._ts


class MVCCStore:
    def __init__(self, clock: Optional["Clock"] = None):
        self.clock = clock or Clock()
        self._next_rid = 0
        self._chains: Dict[int, List[Version]] = {}   # rid -> versions
        self._lock = threading.Lock()             # protects metadata only

    # ---- timestamps ------------------------------------------------------
    def new_timestamp(self) -> int:
        return self.clock.tick()

    def current_ts(self) -> int:
        return self.clock.now()

    # ---- rid allocation --------------------------------------------------
    def _alloc_rid(self) -> int:
        rid = self._next_rid
        self._next_rid += 1
        return rid

    # ---- visibility ------------------------------------------------------
    def _visible(self, v: Version, txn) -> bool:
        if v.creator == txn.txn_id and not v.committed:
            return True  # our own in-flight write
        if not v.committed:
            return False
        if v.begin_ts > txn.read_ts:
            return False
        if v.end_ts is not None and v.end_ts <= txn.read_ts:
            return False
        return True

    def visible_version(self, rid: int, txn) -> Optional[Version]:
        chain = self._chains.get(rid)
        if not chain:
            return None
        # newest visible version wins
        for v in reversed(chain):
            if self._visible(v, txn):
                return v
        return None

    # ---- reads -----------------------------------------------------------
    def get(self, rid: int, txn) -> Optional[bytes]:
        v = self.visible_version(rid, txn)
        if v is None or v.is_deleted():
            return None
        return v.data

    def scan(self, txn) -> Iterator[Tuple[int, bytes]]:
        with self._lock:
            rids = list(self._chains.keys())
        for rid in rids:
            v = self.visible_version(rid, txn)
            if v is not None and not v.is_deleted():
                yield rid, v.data

    # ---- writes ----------------------------------------------------------
    def _head_committed(self, chain: List[Version]) -> Optional[Version]:
        for v in reversed(chain):
            if v.committed:
                return v
        return None

    def _check_conflict(self, chain: List[Version], txn):
        # another active txn holds an uncommitted version -> conflict
        for v in chain:
            if not v.committed and v.creator != txn.txn_id:
                raise MVCCConflict("row locked by another active transaction")
        head = self._head_committed(chain)
        if head is not None and head.begin_ts > txn.read_ts:
            # someone committed a newer version after our snapshot
            raise MVCCConflict("row updated since snapshot")

    def insert(self, data: bytes, txn) -> int:
        """Create a brand-new row; returns its synthetic rid."""
        with self._lock:
            rid = self._alloc_rid()
            v = Version(data, txn.txn_id)
            self._chains[rid] = [v]
        txn.writes.append(("__mvcc__", rid))
        return rid

    def update(self, rid: int, data: Optional[bytes], txn):
        """Append a new version (data=None for a delete tombstone)."""
        with self._lock:
            chain = self._chains.get(rid)
            if chain is None:
                raise KeyError(rid)
            self._check_conflict(chain, txn)
            v = Version(data, txn.txn_id)
            chain.append(v)
        txn.writes.append(("__mvcc__", rid))

    # ---- commit / abort --------------------------------------------------
    def commit(self, txn):
        commit_ts = self.new_timestamp()
        txn.commit_ts = commit_ts
        with self._lock:
            for _tbl, rid in txn.writes:
                chain = self._chains.get(rid)
                if not chain:
                    continue
                for v in chain:
                    if v.creator == txn.txn_id and not v.committed:
                        v.begin_ts = commit_ts
                        v.committed = True
                        # close the previous live committed version
                        for prev in reversed(chain[:chain.index(v)]):
                            if prev.committed and prev.end_ts is None:
                                prev.end_ts = commit_ts
                                break
        return commit_ts

    def abort(self, txn):
        with self._lock:
            for _tbl, rid in txn.writes:
                chain = self._chains.get(rid)
                if not chain:
                    continue
                chain[:] = [v for v in chain
                            if not (v.creator == txn.txn_id and not v.committed)]
                if not chain:
                    self._chains.pop(rid, None)

    def seed(self, data: bytes) -> int:
        """Load a pre-existing committed row (from the persisted heap) as the
        base version of a fresh chain. Used once at startup."""
        with self._lock:
            rid = self._alloc_rid()
            v = Version(data, creator=-1)
            v.begin_ts = 0
            v.committed = True
            self._chains[rid] = [v]
            return rid

    # ---- stats -----------------------------------------------------------
    def stats(self) -> Dict[str, int]:
        with self._lock:
            total_versions = sum(len(c) for c in self._chains.values())
            return {
                "rows": len(self._chains),
                "versions": total_versions,
            }
