"""
Lock Manager — Strict Two-Phase Locking (2PL) for MiniDB.

Implements row-level locking with SHARED and EXCLUSIVE modes.
Includes deadlock detection via wait-for graph cycle detection.

Protocol:
  - Strict 2PL: locks are held until transaction commits/aborts.
  - Lock upgrade: SHARED → EXCLUSIVE is supported.
  - Deadlock detection: periodic or on-demand cycle detection in wait-for graph.
"""

import threading
from enum import Enum, auto
from collections import defaultdict
from typing import Optional, Set, Dict


class LockMode(Enum):
    SHARED = auto()      # Read lock — multiple txns can hold simultaneously
    EXCLUSIVE = auto()   # Write lock — only one txn can hold


class LockRequest:
    """A pending or granted lock request."""

    def __init__(self, txn_id: int, mode: LockMode):
        self.txn_id = txn_id
        self.mode = mode
        self.granted = False
        self.event = threading.Event()

    def __repr__(self):
        status = 'GRANTED' if self.granted else 'WAITING'
        return f"LockRequest(txn={self.txn_id}, mode={self.mode.name}, {status})"


class LockEntry:
    """All lock state for a single resource (e.g., a row)."""

    def __init__(self):
        self.granted: list[LockRequest] = []
        self.waiting: list[LockRequest] = []

    def is_compatible(self, mode: LockMode) -> bool:
        """Check if a new lock mode is compatible with current grants."""
        if not self.granted:
            return True
        if mode == LockMode.SHARED:
            return all(r.mode == LockMode.SHARED for r in self.granted)
        # EXCLUSIVE: no other locks allowed
        return False

    def get_holders(self) -> Set[int]:
        """Get set of txn_ids holding locks."""
        return {r.txn_id for r in self.granted}


class DeadlockError(Exception):
    """Raised when a deadlock is detected."""
    pass


class LockTimeoutError(Exception):
    """Raised when a lock request times out."""
    pass


class LockManager:
    """
    Row-level lock manager implementing Strict Two-Phase Locking.

    Resources are identified by (table_name, rid) tuples.

    Usage:
        lm = LockManager()
        lm.acquire(txn_id=1, resource=('employees', RID(1, 0)), mode=LockMode.SHARED)
        lm.acquire(txn_id=1, resource=('employees', RID(1, 0)), mode=LockMode.EXCLUSIVE)
        lm.release_all(txn_id=1)

    Deadlock Detection:
        Uses a wait-for graph. When txn A waits for a lock held by txn B,
        an edge A → B is added. Cycles indicate deadlocks.
    """

    def __init__(self, deadlock_timeout: float = 5.0):
        """
        Args:
            deadlock_timeout: Seconds to wait before checking for deadlock.
        """
        self._lock_table: Dict[tuple, LockEntry] = {}
        self._txn_locks: Dict[int, Set[tuple]] = defaultdict(set)
        self._wait_for: Dict[int, Set[int]] = defaultdict(set)  # txn -> set of txns it waits for
        self._mutex = threading.Lock()
        self.deadlock_timeout = deadlock_timeout

    def acquire(self, txn_id: int, resource: tuple, mode: LockMode,
                timeout: float = None) -> bool:
        """
        Acquire a lock on a resource.

        Args:
            txn_id: Transaction ID.
            resource: Resource identifier (e.g., ('table', rid)).
            mode: SHARED or EXCLUSIVE.
            timeout: Max seconds to wait (None = use deadlock_timeout).

        Returns:
            True if lock acquired.

        Raises:
            DeadlockError: If a deadlock is detected.
            LockTimeoutError: If the lock could not be acquired in time.
        """
        if timeout is None:
            timeout = self.deadlock_timeout

        with self._mutex:
            if resource not in self._lock_table:
                self._lock_table[resource] = LockEntry()

            entry = self._lock_table[resource]

            # Check if this txn already holds a lock on this resource
            for req in entry.granted:
                if req.txn_id == txn_id:
                    if req.mode == mode or req.mode == LockMode.EXCLUSIVE:
                        return True  # Already have equal or stronger lock
                    # Lock upgrade: SHARED → EXCLUSIVE
                    if mode == LockMode.EXCLUSIVE:
                        # Can upgrade if we're the only holder
                        if len(entry.granted) == 1:
                            req.mode = LockMode.EXCLUSIVE
                            return True
                        else:
                            # Need to wait for other shared locks to release
                            upgrade_req = LockRequest(txn_id, mode)
                            entry.waiting.insert(0, upgrade_req)  # Priority
                            holders = entry.get_holders() - {txn_id}
                            self._wait_for[txn_id] = holders

                            # Check for deadlock before waiting
                            if self._detect_deadlock(txn_id):
                                entry.waiting.remove(upgrade_req)
                                del self._wait_for[txn_id]
                                raise DeadlockError(
                                    f"Deadlock detected: transaction {txn_id} "
                                    f"waiting for {holders}"
                                )

                            self._mutex.release()
                            try:
                                if not upgrade_req.event.wait(timeout):
                                    with self._mutex:
                                        if upgrade_req in entry.waiting:
                                            entry.waiting.remove(upgrade_req)
                                        if txn_id in self._wait_for:
                                            del self._wait_for[txn_id]
                                    raise LockTimeoutError(
                                        f"Lock upgrade timeout for txn {txn_id}")
                            finally:
                                self._mutex.acquire()

                            if txn_id in self._wait_for:
                                del self._wait_for[txn_id]
                            req.mode = LockMode.EXCLUSIVE
                            return True

            # New lock request
            if entry.is_compatible(mode) and not entry.waiting:
                # Grant immediately
                req = LockRequest(txn_id, mode)
                req.granted = True
                entry.granted.append(req)
                self._txn_locks[txn_id].add(resource)
                return True
            else:
                # Must wait
                req = LockRequest(txn_id, mode)
                entry.waiting.append(req)

                holders = entry.get_holders()
                self._wait_for[txn_id] = holders

                if self._detect_deadlock(txn_id):
                    entry.waiting.remove(req)
                    del self._wait_for[txn_id]
                    raise DeadlockError(
                        f"Deadlock detected: transaction {txn_id} "
                        f"waiting for {holders}"
                    )

                self._mutex.release()
                try:
                    if not req.event.wait(timeout):
                        with self._mutex:
                            if req in entry.waiting:
                                entry.waiting.remove(req)
                            if txn_id in self._wait_for:
                                del self._wait_for[txn_id]
                        raise LockTimeoutError(
                            f"Lock timeout for txn {txn_id} on {resource}")
                finally:
                    self._mutex.acquire()

                if txn_id in self._wait_for:
                    del self._wait_for[txn_id]

                self._txn_locks[txn_id].add(resource)
                return True

    def release(self, txn_id: int, resource: tuple):
        """Release a specific lock held by a transaction."""
        with self._mutex:
            if resource not in self._lock_table:
                return

            entry = self._lock_table[resource]

            # Remove granted lock
            entry.granted = [r for r in entry.granted if r.txn_id != txn_id]

            if txn_id in self._txn_locks:
                self._txn_locks[txn_id].discard(resource)

            # Try to grant waiting requests
            self._grant_waiting(entry)

    def release_all(self, txn_id: int):
        """
        Release all locks held by a transaction (called on COMMIT/ABORT).

        This is the key part of Strict 2PL — all locks are released together.
        """
        with self._mutex:
            resources = list(self._txn_locks.get(txn_id, set()))
            for resource in resources:
                if resource in self._lock_table:
                    entry = self._lock_table[resource]
                    entry.granted = [r for r in entry.granted if r.txn_id != txn_id]
                    # Also remove from waiting
                    entry.waiting = [r for r in entry.waiting if r.txn_id != txn_id]
                    self._grant_waiting(entry)

            if txn_id in self._txn_locks:
                del self._txn_locks[txn_id]
            if txn_id in self._wait_for:
                del self._wait_for[txn_id]

    def _grant_waiting(self, entry: LockEntry):
        """Try to grant waiting lock requests."""
        newly_granted = []
        remaining = []

        for req in entry.waiting:
            if entry.is_compatible(req.mode):
                req.granted = True
                entry.granted.append(req)
                req.event.set()
                newly_granted.append(req)
            else:
                remaining.append(req)
                break  # FIFO: stop at first incompatible request

        entry.waiting = [r for r in entry.waiting if r not in newly_granted]

    def _detect_deadlock(self, start_txn: int) -> bool:
        """
        Detect deadlock using DFS on the wait-for graph.

        Returns True if a cycle is found involving start_txn.
        """
        visited = set()
        stack = set()

        def dfs(txn):
            if txn in stack:
                return True  # Cycle found
            if txn in visited:
                return False

            visited.add(txn)
            stack.add(txn)

            for waited_txn in self._wait_for.get(txn, set()):
                if dfs(waited_txn):
                    return True

            stack.discard(txn)
            return False

        return dfs(start_txn)

    def get_lock_info(self) -> dict:
        """Get current lock state for debugging."""
        with self._mutex:
            info = {}
            for resource, entry in self._lock_table.items():
                if entry.granted or entry.waiting:
                    info[str(resource)] = {
                        'granted': [str(r) for r in entry.granted],
                        'waiting': [str(r) for r in entry.waiting],
                    }
            return info

    def get_txn_locks(self, txn_id: int) -> Set[tuple]:
        """Get all resources locked by a transaction."""
        with self._mutex:
            return set(self._txn_locks.get(txn_id, set()))
