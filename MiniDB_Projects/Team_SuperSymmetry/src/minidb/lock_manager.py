"""
Thread-safe lock manager for Strict Two-Phase Locking (Strict 2PL).

Row-granularity shared (S) / exclusive (X) locks, identified by an opaque
resource id (e.g. (table, rid)). Locks are held until commit/abort (strict
2PL) -> conflict-serializable, strict (recoverable, cascadeless) schedules.

This is a genuinely *blocking* manager: a transaction that requests a
conflicting lock waits on a condition variable until the lock is released.
Before a transaction is allowed to wait, the manager checks the global
wait-for graph for a cycle; if waiting would deadlock, a DeadlockError is
raised and the requesting transaction becomes the victim.
"""
from __future__ import annotations

import threading
from collections import defaultdict
from enum import Enum
from typing import Dict, List, Set


class LockMode(Enum):
    S = "S"
    X = "X"


class DeadlockError(Exception):
    pass


class _Resource:
    __slots__ = ("holders", "waiters")

    def __init__(self):
        self.holders: Dict[int, LockMode] = {}   # txn -> mode
        self.waiters: List[int] = []             # txns queued (FIFO)


class LockManager:
    def __init__(self):
        self._mutex = threading.Condition()
        self._res: Dict[object, _Resource] = defaultdict(_Resource)
        self._held_by: Dict[int, Set[object]] = defaultdict(set)
        # wait-for graph: txn -> set of txns it is currently waiting on
        self._waits_for: Dict[int, Set[int]] = defaultdict(set)

    # ---- helpers (call with mutex held) ---------------------------------
    def _compatible(self, res: _Resource, txn_id: int, mode: LockMode) -> bool:
        for other, omode in res.holders.items():
            if other == txn_id:
                continue
            if mode == LockMode.X or omode == LockMode.X:
                return False
        return True

    def _creates_cycle(self, txn_id: int, blockers: Set[int]) -> bool:
        # would txn_id -> blockers introduce a cycle reaching txn_id?
        seen: Set[int] = set()
        stack = list(blockers - {txn_id})
        while stack:
            t = stack.pop()
            if t == txn_id:
                return True
            if t in seen:
                continue
            seen.add(t)
            stack.extend(self._waits_for.get(t, ()))
        return False

    # ---- public ----------------------------------------------------------
    def acquire(self, txn_id: int, resource, mode: LockMode, timeout: float = 5.0):
        with self._mutex:
            res = self._res[resource]
            # upgrade or re-entrant
            if txn_id in res.holders:
                if res.holders[txn_id] == LockMode.X or mode == LockMode.S:
                    return
                # S -> X upgrade
                while not self._compatible(res, txn_id, LockMode.X):
                    blockers = {h for h in res.holders if h != txn_id}
                    if self._creates_cycle(txn_id, blockers):
                        raise DeadlockError(
                            f"deadlock: txn {txn_id} upgrading {resource}")
                    self._waits_for[txn_id] = blockers
                    if not self._mutex.wait(timeout):
                        self._waits_for.pop(txn_id, None)
                        raise DeadlockError(
                            f"timeout: txn {txn_id} upgrading {resource}")
                self._waits_for.pop(txn_id, None)
                res.holders[txn_id] = LockMode.X
                return

            while not self._compatible(res, txn_id, mode):
                blockers = {h for h in res.holders if h != txn_id}
                if self._creates_cycle(txn_id, blockers):
                    raise DeadlockError(
                        f"deadlock: txn {txn_id} waiting for {resource} "
                        f"held by {sorted(blockers)}")
                self._waits_for[txn_id] = blockers
                if txn_id not in res.waiters:
                    res.waiters.append(txn_id)
                if not self._mutex.wait(timeout):
                    self._waits_for.pop(txn_id, None)
                    if txn_id in res.waiters:
                        res.waiters.remove(txn_id)
                    raise DeadlockError(
                        f"timeout: txn {txn_id} waiting for {resource}")
            self._waits_for.pop(txn_id, None)
            if txn_id in res.waiters:
                res.waiters.remove(txn_id)
            res.holders[txn_id] = mode
            self._held_by[txn_id].add(resource)

    def release_all(self, txn_id: int):
        with self._mutex:
            for resource in list(self._held_by.get(txn_id, ())):
                res = self._res.get(resource)
                if res:
                    res.holders.pop(txn_id, None)
            self._held_by.pop(txn_id, None)
            self._waits_for.pop(txn_id, None)
            # this txn may have been someone's blocker -> wake everyone
            self._mutex.notify_all()
