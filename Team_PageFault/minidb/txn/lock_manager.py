"""Lock manager implementing Strict Two-Phase Locking (Strict 2PL).

* Two lock modes: SHARED (read) and EXCLUSIVE (write).
* Compatibility: S/S compatible; everything else conflicts.
* Strict 2PL: locks are acquired as needed (growing phase) and *all* released
  together at commit/abort (shrinking phase happens atomically at end), which
  gives serializable isolation and recoverable schedules.
* Deadlock detection: before a transaction waits, we add its edges to a
  wait-for graph and run cycle detection. If waiting would create a cycle, the
  requesting transaction is chosen as the victim and aborted (raises
  ``DeadlockError``), breaking the cycle.

The manager is thread-safe so the concurrency demo can run real transactions in
separate threads.
"""

import logging
import threading
from collections import defaultdict

log = logging.getLogger("minidb.locks")

SHARED = "S"
EXCLUSIVE = "X"


# RuntimeError subclass so the auto-commit path's `except RuntimeError` catches it.
class DeadlockError(RuntimeError):
    pass


class _LockEntry:
    def __init__(self):
        self.mode = None                 # current granted mode (S or X)
        self.holders = set()             # txn_ids holding the lock
        self.queue = []                  # waiting (txn_id, mode) in arrival order


class LockManager:
    def __init__(self):
        self._mutex = threading.RLock()
        self._cv = threading.Condition(self._mutex)
        self._locks = defaultdict(_LockEntry)      # resource -> _LockEntry
        self._held = defaultdict(dict)             # txn_id -> {resource: mode}
        self._waits_for = defaultdict(set)         # txn_id -> set(txn_id)

    def acquire(self, txn_id, resource, mode):
        with self._cv:
            # Fast path: already hold a strong-enough lock.
            held_mode = self._held[txn_id].get(resource)
            if held_mode == EXCLUSIVE or held_mode == mode:
                return
            while not self._compatible(resource, txn_id, mode):
                holders = self._locks[resource].holders - {txn_id}
                self._waits_for[txn_id] = set(holders)
                if self._creates_cycle(txn_id):
                    self._waits_for.pop(txn_id, None)
                    log.debug("deadlock: txn %s waiting on %s would cycle; aborting it",
                              txn_id, resource)
                    raise DeadlockError(
                        f"transaction {txn_id} aborted to break a deadlock")
                self._cv.wait()
            self._waits_for.pop(txn_id, None)
            entry = self._locks[resource]
            entry.holders.add(txn_id)
            # reached only once compatible, so an X request is now the sole holder.
            if mode == EXCLUSIVE:
                entry.mode = EXCLUSIVE
            elif entry.mode is None:
                entry.mode = SHARED
            self._held[txn_id][resource] = (
                EXCLUSIVE if (mode == EXCLUSIVE or held_mode == EXCLUSIVE) else SHARED)

    def _compatible(self, resource, txn_id, mode):
        entry = self._locks[resource]
        others = entry.holders - {txn_id}
        if not others:
            return True
        if mode == SHARED and entry.mode == SHARED:
            return True            # S compatible with existing S holders
        return False               # any X involvement conflicts

    def release_all(self, txn_id):
        """Release every lock held by ``txn_id`` (commit/abort)."""
        with self._cv:
            for resource in list(self._held.get(txn_id, {})):
                entry = self._locks[resource]
                entry.holders.discard(txn_id)
                if not entry.holders:
                    entry.mode = None
            self._held.pop(txn_id, None)
            self._waits_for.pop(txn_id, None)
            for s in self._waits_for.values():
                s.discard(txn_id)
            # notify_all: every waiter must re-check, else a stale wait-for edge
            # can fake a deadlock.
            self._cv.notify_all()

    def _creates_cycle(self, start):
        """DFS over the wait-for graph to detect a cycle reachable from start."""
        visited = set()
        stack = [start]
        while stack:
            node = stack.pop()
            for nxt in self._waits_for.get(node, ()):
                if nxt == start:
                    return True
                if nxt not in visited:
                    visited.add(nxt)
                    stack.append(nxt)
        return False
