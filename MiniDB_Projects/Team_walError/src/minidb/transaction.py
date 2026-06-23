"""transaction.py — transactions providing serializable isolation via strict 2PL.

A Transaction acquires shared locks before reads and exclusive locks before
writes, and holds them all until commit/abort (strict two-phase locking), which
yields serializable schedules and avoids cascading aborts.

The TransactionManager hands out monotonically increasing transaction ids and
tracks the active set. Locking is at table granularity in MiniDB (resource =
table name) — coarse but unambiguously serializable and easy to reason about.

WAL integration (see wal.py) is layered on top by the engine: writes are logged
before being applied, and COMMIT is logged + flushed at commit time.
"""

from __future__ import annotations

import itertools
import threading
from enum import Enum

from .lock_manager import LockManager, LockMode


class TxnState(Enum):
    ACTIVE = "active"
    COMMITTED = "committed"
    ABORTED = "aborted"


class Transaction:
    def __init__(self, txn_id: int, lock_manager: LockManager) -> None:
        self.txn_id = txn_id
        self._lm = lock_manager
        self.state = TxnState.ACTIVE
        self.locks: set[str] = set()

    def lock_shared(self, resource: str) -> None:
        self._require_active()
        self._lm.acquire(self.txn_id, resource, LockMode.SHARED)
        self.locks.add(resource)

    def lock_exclusive(self, resource: str) -> None:
        self._require_active()
        self._lm.acquire(self.txn_id, resource, LockMode.EXCLUSIVE)
        self.locks.add(resource)

    def commit(self) -> None:
        if self.state is not TxnState.ACTIVE:
            return
        self._lm.release_all(self.txn_id)
        self.locks.clear()
        self.state = TxnState.COMMITTED

    def abort(self) -> None:
        if self.state is TxnState.COMMITTED:
            raise RuntimeError("cannot abort a committed transaction")
        self._lm.release_all(self.txn_id)
        self.locks.clear()
        self.state = TxnState.ABORTED

    def _require_active(self) -> None:
        if self.state is not TxnState.ACTIVE:
            raise RuntimeError(f"transaction {self.txn_id} is {self.state.value}")


class TransactionManager:
    def __init__(self, lock_manager: LockManager | None = None) -> None:
        self.lock_manager = lock_manager or LockManager()
        self._ids = itertools.count(1)
        self._lock = threading.Lock()
        self.active: dict[int, Transaction] = {}

    def begin(self) -> Transaction:
        with self._lock:
            txn = Transaction(next(self._ids), self.lock_manager)
            self.active[txn.txn_id] = txn
            return txn

    def commit(self, txn: Transaction) -> None:
        txn.commit()
        with self._lock:
            self.active.pop(txn.txn_id, None)

    def abort(self, txn: Transaction) -> None:
        txn.abort()
        with self._lock:
            self.active.pop(txn.txn_id, None)
