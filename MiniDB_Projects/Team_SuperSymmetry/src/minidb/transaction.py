"""
Transaction objects and identifiers.

A Transaction carries the state shared between the executor, the lock
manager (for 2PL) and the MVCC version store (for snapshot isolation):

  * txn_id       - monotonic identifier, also used as a lock-owner id
  * isolation    - "2PL" (strict two-phase locking, serializable) or "MVCC"
  * state        - ACTIVE / COMMITTED / ABORTED
  * read_ts      - snapshot timestamp (MVCC reads see <= read_ts)
  * commit_ts    - assigned at commit time (MVCC version stamping)
  * undo         - in-memory undo actions for index maintenance on abort
  * writes       - (table, rid) touched, for MVCC validation / cleanup

The object is deliberately a thin record; all policy lives in the Database
and the execution context.
"""
from __future__ import annotations

from typing import Any, Callable, List, Optional, Set, Tuple

ACTIVE = "ACTIVE"
COMMITTED = "COMMITTED"
ABORTED = "ABORTED"


class Transaction:
    def __init__(self, txn_id: int, isolation: str, read_ts: Optional[int] = None):
        self.txn_id = txn_id
        self.isolation = isolation
        self.state = ACTIVE
        self.read_ts = read_ts
        self.commit_ts: Optional[int] = None
        # undo actions run (in reverse) if the txn aborts; each is a callable
        self.undo: List[Callable[[], None]] = []
        # rows this txn has created/modified in the MVCC store
        self.writes: List[Tuple[str, int]] = []
        self.autocommit = False

    def add_undo(self, fn: Callable[[], None]):
        self.undo.append(fn)

    def is_active(self) -> bool:
        return self.state == ACTIVE

    def __repr__(self):
        return (f"Txn(id={self.txn_id}, iso={self.isolation}, "
                f"state={self.state})")
