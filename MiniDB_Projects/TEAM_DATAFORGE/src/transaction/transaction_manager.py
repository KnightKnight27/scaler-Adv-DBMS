"""
Transaction Manager — Manages transaction lifecycle for MiniDB.

Handles BEGIN, COMMIT, ROLLBACK and coordinates with the lock manager
and recovery manager.

Transaction States:
  ACTIVE → COMMITTED
  ACTIVE → ABORTED

Implements Strict Two-Phase Locking:
  - Locks are acquired during transaction execution.
  - All locks are released only at COMMIT or ABORT.
"""

import threading
from enum import Enum, auto
from typing import Optional, Dict

from .lock_manager import LockManager, LockMode


class TransactionState(Enum):
    ACTIVE = auto()
    COMMITTED = auto()
    ABORTED = auto()


class Transaction:
    """Represents a single transaction."""

    def __init__(self, txn_id: int):
        self.txn_id = txn_id
        self.state = TransactionState.ACTIVE
        self.start_ts = None       # For MVCC snapshot
        self.undo_log = []          # [(operation, table, rid, old_values), ...]

    def is_active(self) -> bool:
        return self.state == TransactionState.ACTIVE

    def __repr__(self):
        return f"Transaction(id={self.txn_id}, state={self.state.name})"


class TransactionManager:
    """
    Manages transaction lifecycle and coordinates with lock/recovery managers.

    Usage:
        tm = TransactionManager(lock_manager)
        txn_id = tm.begin()
        # ... perform operations ...
        tm.commit(txn_id)

    Thread Safety:
        All methods are thread-safe.
    """

    def __init__(self, lock_manager: LockManager, recovery_manager=None):
        """
        Args:
            lock_manager: The lock manager for 2PL.
            recovery_manager: Optional recovery manager for WAL logging.
        """
        self.lock_manager = lock_manager
        self.recovery_manager = recovery_manager
        self._transactions: Dict[int, Transaction] = {}
        self._next_txn_id = 1
        self._lock = threading.Lock()
        self._active_txns: set = set()
        self._global_ts = 0  # Global timestamp counter for MVCC

    def begin(self) -> int:
        """
        Begin a new transaction.

        Returns:
            The transaction ID.
        """
        with self._lock:
            txn_id = self._next_txn_id
            self._next_txn_id += 1
            self._global_ts += 1

            txn = Transaction(txn_id)
            txn.start_ts = self._global_ts
            self._transactions[txn_id] = txn
            self._active_txns.add(txn_id)

            # WAL: log BEGIN
            if self.recovery_manager:
                self.recovery_manager.log_begin(txn_id)

            return txn_id

    def commit(self, txn_id: int):
        """
        Commit a transaction — release all locks and mark as committed.

        Args:
            txn_id: Transaction ID to commit.

        Raises:
            ValueError: If transaction doesn't exist or isn't active.
        """
        with self._lock:
            txn = self._get_active_txn(txn_id)

            # WAL: log COMMIT (force to disk before releasing locks)
            if self.recovery_manager:
                self.recovery_manager.log_commit(txn_id)
                self.recovery_manager.flush()

            txn.state = TransactionState.COMMITTED
            self._active_txns.discard(txn_id)

        # Release all locks (outside the lock to avoid deadlock with lock manager)
        self.lock_manager.release_all(txn_id)

    def abort(self, txn_id: int):
        """
        Abort a transaction — undo changes, release locks.

        Args:
            txn_id: Transaction ID to abort.
        """
        with self._lock:
            txn = self._get_active_txn(txn_id)

            # WAL: log ABORT
            if self.recovery_manager:
                self.recovery_manager.log_abort(txn_id)

            txn.state = TransactionState.ABORTED
            self._active_txns.discard(txn_id)

        # Release all locks
        self.lock_manager.release_all(txn_id)

    def rollback(self, txn_id: int):
        """Alias for abort."""
        self.abort(txn_id)

    def get_transaction(self, txn_id: int) -> Optional[Transaction]:
        """Get a transaction by ID."""
        return self._transactions.get(txn_id)

    def is_active(self, txn_id: int) -> bool:
        """Check if a transaction is active."""
        txn = self._transactions.get(txn_id)
        return txn is not None and txn.is_active()

    def get_active_transactions(self) -> set:
        """Get set of active transaction IDs."""
        with self._lock:
            return set(self._active_txns)

    def get_snapshot_ts(self, txn_id: int) -> int:
        """Get the snapshot timestamp for MVCC."""
        txn = self._transactions.get(txn_id)
        return txn.start_ts if txn else 0

    def get_current_ts(self) -> int:
        """Get the current global timestamp."""
        with self._lock:
            return self._global_ts

    def next_timestamp(self) -> int:
        """Advance and return the next global timestamp."""
        with self._lock:
            self._global_ts += 1
            return self._global_ts

    def add_undo_entry(self, txn_id: int, operation: str, table: str,
                        rid=None, old_values=None):
        """Add an undo log entry for rollback."""
        txn = self._transactions.get(txn_id)
        if txn and txn.is_active():
            txn.undo_log.append((operation, table, rid, old_values))

    def _get_active_txn(self, txn_id: int) -> Transaction:
        """Get an active transaction, raising if invalid."""
        txn = self._transactions.get(txn_id)
        if txn is None:
            raise ValueError(f"Transaction {txn_id} does not exist")
        if not txn.is_active():
            raise ValueError(f"Transaction {txn_id} is not active (state={txn.state.name})")
        return txn

    def get_stats(self) -> dict:
        """Get transaction manager statistics."""
        with self._lock:
            return {
                'total_transactions': len(self._transactions),
                'active_transactions': len(self._active_txns),
                'committed': sum(1 for t in self._transactions.values()
                               if t.state == TransactionState.COMMITTED),
                'aborted': sum(1 for t in self._transactions.values()
                             if t.state == TransactionState.ABORTED),
                'global_timestamp': self._global_ts,
            }
