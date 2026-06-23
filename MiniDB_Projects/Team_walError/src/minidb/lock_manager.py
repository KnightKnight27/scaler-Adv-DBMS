"""lock_manager.py — a 2PL lock manager with wait-for-graph deadlock detection.

Locks are taken on opaque string "resources" (MiniDB uses table names, but the
manager is agnostic). Two modes:
    SHARED    (read)  — many transactions may hold it together
    EXCLUSIVE (write) — held by at most one transaction, conflicts with all

Compatibility:
              held SHARED   held EXCLUSIVE
    want S       yes             no
    want X        no             no

Blocking + deadlock: `acquire` blocks (on a condition variable) until the lock is
grantable. Before blocking it records the request in a wait-for graph and runs a
cycle check; if granting would create a deadlock, the *requesting* transaction is
chosen as the victim and a DeadlockError is raised instead of blocking forever.
Lock upgrade (a transaction holding S requesting X) is supported when it is the
sole holder.

Thread-safe: all state is guarded by a single Condition.
"""

from __future__ import annotations

import threading
from collections import defaultdict
from enum import Enum


class LockMode(Enum):
    SHARED = "S"
    EXCLUSIVE = "X"


class DeadlockError(Exception):
    """Raised in the victim transaction when acquiring a lock would deadlock."""

    def __init__(self, txn_id: int, resource: str) -> None:
        super().__init__(f"deadlock detected: txn {txn_id} aborted on {resource!r}")
        self.txn_id = txn_id
        self.resource = resource


def _conflict(a: LockMode, b: LockMode) -> bool:
    return a is LockMode.EXCLUSIVE or b is LockMode.EXCLUSIVE


class LockManager:
    def __init__(self) -> None:
        self._cond = threading.Condition()
        self._granted: dict[str, dict[int, LockMode]] = defaultdict(dict)
        self._waiting: dict[str, list[tuple[int, LockMode]]] = defaultdict(list)

    # --- public API --------------------------------------------------------

    def acquire(self, txn_id: int, resource: str, mode: LockMode) -> None:
        """Acquire `mode` lock on `resource` for `txn_id`, blocking if needed.

        Raises DeadlockError if waiting would create a cycle.
        """
        with self._cond:
            held = self._granted[resource].get(txn_id)
            if held is LockMode.EXCLUSIVE or held is mode:
                return  # already hold an equal-or-stronger lock
            while True:
                if self._can_grant(resource, txn_id, mode):
                    self._granted[resource][txn_id] = mode
                    self._remove_waiting(resource, txn_id)
                    self._cond.notify_all()
                    return
                # cannot grant yet -> record the wait and look for a deadlock
                if (txn_id, mode) not in self._waiting[resource]:
                    self._waiting[resource].append((txn_id, mode))
                if self._creates_cycle(txn_id):
                    self._remove_waiting(resource, txn_id)
                    self._cond.notify_all()
                    raise DeadlockError(txn_id, resource)
                self._cond.wait()

    def release(self, txn_id: int, resource: str) -> None:
        with self._cond:
            self._granted[resource].pop(txn_id, None)
            self._remove_waiting(resource, txn_id)
            self._cond.notify_all()

    def release_all(self, txn_id: int) -> None:
        """Release every lock held or requested by a transaction (commit/abort)."""
        with self._cond:
            for holders in self._granted.values():
                holders.pop(txn_id, None)
            for resource in list(self._waiting):
                self._waiting[resource] = [
                    (t, m) for (t, m) in self._waiting[resource] if t != txn_id
                ]
            self._cond.notify_all()

    # --- introspection (used by tests / demos) -----------------------------

    def holders(self, resource: str) -> dict[int, LockMode]:
        with self._cond:
            return dict(self._granted[resource])

    def wait_for_graph(self) -> dict[int, set[int]]:
        with self._cond:
            return self._build_wait_for()

    # --- internals (call only while holding self._cond) --------------------

    def _can_grant(self, resource: str, txn_id: int, mode: LockMode) -> bool:
        others = {t: m for t, m in self._granted[resource].items() if t != txn_id}
        if not others:
            return True
        if mode is LockMode.EXCLUSIVE:
            return False  # exclusive needs to be the sole holder
        # want SHARED: ok only if everyone else also holds SHARED
        return all(m is LockMode.SHARED for m in others.values())

    def _remove_waiting(self, resource: str, txn_id: int) -> None:
        self._waiting[resource] = [
            (t, m) for (t, m) in self._waiting[resource] if t != txn_id
        ]

    def _build_wait_for(self) -> dict[int, set[int]]:
        graph: dict[int, set[int]] = defaultdict(set)
        for resource, waiters in self._waiting.items():
            holders = self._granted[resource]
            for tw, mw in waiters:
                for h, hm in holders.items():
                    if h != tw and _conflict(mw, hm):
                        graph[tw].add(h)
        return graph

    def _creates_cycle(self, start: int) -> bool:
        graph = self._build_wait_for()
        # DFS looking for a path from `start` back to `start`
        seen: set[int] = set()
        stack = list(graph.get(start, ()))
        while stack:
            node = stack.pop()
            if node == start:
                return True
            if node in seen:
                continue
            seen.add(node)
            stack.extend(graph.get(node, ()))
        return False
