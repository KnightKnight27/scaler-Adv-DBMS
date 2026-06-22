from __future__ import annotations

import threading
import time
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Callable

from .types import LockType, ResourceID, TransactionID, TransactionMode, TransactionState


class DeadlockError(RuntimeError):
    pass


class TransactionAbortedError(RuntimeError):
    pass


class WALManager:
    def log_write(self, txn_id: TransactionID, resource_id: ResourceID, action: str) -> None:
        return None

    def before_commit(self, txn_id: TransactionID) -> None:
        return None

    def on_abort(self, txn_id: TransactionID) -> None:
        return None


class RecoveryManager:
    def rollback(self, txn_id: TransactionID) -> None:
        return None


class VersionStore:
    def record_version(self, txn_id: TransactionID, resource_id: ResourceID) -> None:
        return None


class MVCCManager:
    def __init__(self, version_store: VersionStore | None = None):
        self.version_store = version_store or VersionStore()

    def before_read(self, txn_id: TransactionID, resource_id: ResourceID) -> None:
        return None

    def before_write(self, txn_id: TransactionID, resource_id: ResourceID) -> None:
        self.version_store.record_version(txn_id, resource_id)

    def commit(self, txn_id: TransactionID) -> None:
        return None

    def rollback(self, txn_id: TransactionID) -> None:
        return None


@dataclass(slots=True)
class LockGrant:
    txn_id: TransactionID
    lock_type: LockType


class LockManager:
    def __init__(self):
        self._condition = threading.Condition()
        self._grants: dict[ResourceID, list[LockGrant]] = defaultdict(list)
        self._waiting: dict[TransactionID, tuple[ResourceID, LockType]] = {}
        self._waits_for: dict[TransactionID, set[TransactionID]] = defaultdict(set)
        self.logs: list[str] = []
        self._aborted: set[TransactionID] = set()

    def acquire(self, txn_id: TransactionID, resource_id: ResourceID, lock_type: LockType) -> None:
        wait_logged = False
        with self._condition:
            while True:
                if txn_id in self._aborted:
                    raise TransactionAbortedError(f"Transaction {txn_id} has been aborted.")
                if self._can_grant(txn_id, resource_id, lock_type):
                    self._grant(txn_id, resource_id, lock_type)
                    self._waiting.pop(txn_id, None)
                    self._waits_for.pop(txn_id, None)
                    self.logs.append(
                        f"lock acquired txn={txn_id} resource={resource_id} mode={lock_type.value}"
                    )
                    self._condition.notify_all()
                    return
                holders = {
                    lock.txn_id
                    for lock in self._grants[resource_id]
                    if lock.txn_id != txn_id and self._conflicts(lock.lock_type, lock_type)
                }
                self._waiting[txn_id] = (resource_id, lock_type)
                self._waits_for[txn_id] = holders
                if not wait_logged:
                    self.logs.append(
                        f"lock waiting txn={txn_id} resource={resource_id} mode={lock_type.value}"
                    )
                    wait_logged = True
                cycle = self._detect_cycle()
                if cycle:
                    victim = max(cycle)
                    self.logs.append(
                        f"deadlock detected cycle={cycle} victim={victim}"
                    )
                    self._aborted.add(victim)
                    self._condition.notify_all()
                    if victim == txn_id:
                        raise DeadlockError(
                            f"Deadlock detected for transaction {txn_id}; victim={victim}."
                        )
                self._condition.wait(timeout=0.05)

    def release_transaction(self, txn_id: TransactionID) -> None:
        with self._condition:
            for resource_id, grants in list(self._grants.items()):
                retained = [grant for grant in grants if grant.txn_id != txn_id]
                removed_count = len(grants) - len(retained)
                if removed_count:
                    self.logs.append(f"lock released txn={txn_id} resource={resource_id}")
                if retained:
                    self._grants[resource_id] = retained
                else:
                    self._grants.pop(resource_id, None)
            self._waiting.pop(txn_id, None)
            self._waits_for.pop(txn_id, None)
            self._aborted.discard(txn_id)
            for waiters in self._waits_for.values():
                waiters.discard(txn_id)
            self._condition.notify_all()

    def current_holders(self, resource_id: ResourceID) -> list[LockGrant]:
        with self._condition:
            return list(self._grants.get(resource_id, []))

    def mark_aborted(self, txn_id: TransactionID) -> None:
        with self._condition:
            self._aborted.add(txn_id)
            self._condition.notify_all()

    def _can_grant(self, txn_id: TransactionID, resource_id: ResourceID, lock_type: LockType) -> bool:
        grants = self._grants[resource_id]
        if not grants:
            return True
        other_grants = [grant for grant in grants if grant.txn_id != txn_id]
        if not other_grants:
            if lock_type == LockType.SHARED:
                return True
            return True
        if lock_type == LockType.SHARED:
            return all(grant.lock_type == LockType.SHARED for grant in other_grants)
        return False

    def _grant(self, txn_id: TransactionID, resource_id: ResourceID, lock_type: LockType) -> None:
        grants = self._grants[resource_id]
        for grant in grants:
            if grant.txn_id == txn_id:
                if grant.lock_type == LockType.SHARED and lock_type == LockType.EXCLUSIVE:
                    grant.lock_type = LockType.EXCLUSIVE
                return
        grants.append(LockGrant(txn_id=txn_id, lock_type=lock_type))

    @staticmethod
    def _conflicts(existing: LockType, requested: LockType) -> bool:
        return not (existing == LockType.SHARED and requested == LockType.SHARED)

    def _detect_cycle(self) -> list[TransactionID] | None:
        visited: set[TransactionID] = set()
        stack: list[TransactionID] = []
        in_stack: set[TransactionID] = set()

        def dfs(node: TransactionID) -> list[TransactionID] | None:
            visited.add(node)
            stack.append(node)
            in_stack.add(node)
            for neighbor in sorted(self._waits_for.get(node, set())):
                if neighbor not in visited:
                    cycle = dfs(neighbor)
                    if cycle:
                        return cycle
                elif neighbor in in_stack:
                    start = stack.index(neighbor)
                    return stack[start:]
            stack.pop()
            in_stack.remove(node)
            return None

        for node in sorted(self._waits_for):
            if node not in visited:
                cycle = dfs(node)
                if cycle:
                    return cycle
        return None


@dataclass(slots=True)
class Transaction:
    txn_id: TransactionID
    mode: TransactionMode
    state: TransactionState = TransactionState.ACTIVE
    started_at: float = field(default_factory=time.time)
    undo_actions: list[Callable[[], None]] = field(default_factory=list)


class TransactionManager:
    def __init__(
        self,
        *,
        lock_manager: LockManager | None = None,
        wal_manager: WALManager | None = None,
        recovery_manager: RecoveryManager | None = None,
        mvcc_manager: MVCCManager | None = None,
        default_mode: TransactionMode = TransactionMode.TWO_PL,
    ):
        self.lock_manager = lock_manager or LockManager()
        self.wal_manager = wal_manager or WALManager()
        self.recovery_manager = recovery_manager or RecoveryManager()
        self.mvcc_manager = mvcc_manager or MVCCManager()
        self.default_mode = default_mode
        self.transactions: dict[TransactionID, Transaction] = {}
        self.logs: list[str] = []
        self._next_txn_id = 1

    def begin(self) -> TransactionID:
        txn_id = self._next_txn_id
        self._next_txn_id += 1
        self.transactions[txn_id] = Transaction(txn_id=txn_id, mode=self.default_mode)
        self.logs.append(f"transaction begun txn={txn_id} mode={self.default_mode.value}")
        return txn_id

    def set_mode(self, mode: TransactionMode) -> None:
        self.default_mode = mode
        self.logs.append(f"transaction mode set mode={mode.value}")

    def before_read(self, txn_id: TransactionID | None, resource_id: ResourceID) -> None:
        if txn_id is None:
            return
        txn = self._require_active_transaction(txn_id)
        if txn.mode == TransactionMode.MVCC:
            self.mvcc_manager.before_read(txn_id, resource_id)
            return
        self._acquire_lock(txn_id, resource_id, LockType.SHARED)

    def before_write(self, txn_id: TransactionID | None, resource_id: ResourceID) -> None:
        if txn_id is None:
            return
        txn = self._require_active_transaction(txn_id)
        if txn.mode == TransactionMode.MVCC:
            self.mvcc_manager.before_write(txn_id, resource_id)
            return
        self._acquire_lock(txn_id, resource_id, LockType.EXCLUSIVE)

    def beforeRead(self, txn_id: TransactionID | None, resource_id: ResourceID) -> None:
        self.before_read(txn_id, resource_id)

    def beforeWrite(self, txn_id: TransactionID | None, resource_id: ResourceID) -> None:
        self.before_write(txn_id, resource_id)

    def register_undo(self, txn_id: TransactionID | None, undo_action: Callable[[], None]) -> None:
        if txn_id is None:
            return
        txn = self._require_active_transaction(txn_id)
        txn.undo_actions.append(undo_action)

    def commit(self, txn_id: TransactionID) -> None:
        txn = self._require_active_transaction(txn_id)
        self.wal_manager.before_commit(txn_id)
        if txn.mode == TransactionMode.MVCC:
            self.mvcc_manager.commit(txn_id)
        txn.undo_actions.clear()
        self.lock_manager.release_transaction(txn_id)
        txn.state = TransactionState.COMMITTED
        self.logs.append(f"transaction committed txn={txn_id}")

    def rollback(self, txn_id: TransactionID) -> None:
        txn = self._get_transaction(txn_id)
        if txn.state != TransactionState.ACTIVE:
            return
        for undo_action in reversed(txn.undo_actions):
            undo_action()
        txn.undo_actions.clear()
        if txn.mode == TransactionMode.MVCC:
            self.mvcc_manager.rollback(txn_id)
        self.recovery_manager.rollback(txn_id)
        self.wal_manager.on_abort(txn_id)
        self.lock_manager.release_transaction(txn_id)
        txn.state = TransactionState.ABORTED
        self.logs.append(f"transaction aborted txn={txn_id}")

    def rollback_due_to_deadlock(self, txn_id: TransactionID) -> None:
        if txn_id in self.transactions and self.transactions[txn_id].state == TransactionState.ACTIVE:
            self.lock_manager.mark_aborted(txn_id)
            self.rollback(txn_id)

    def get_state(self, txn_id: TransactionID) -> TransactionState:
        return self._get_transaction(txn_id).state

    def _acquire_lock(self, txn_id: TransactionID, resource_id: ResourceID, lock_type: LockType) -> None:
        try:
            self.lock_manager.acquire(txn_id, resource_id, lock_type)
        except DeadlockError:
            self.rollback_due_to_deadlock(txn_id)
            raise
        except TransactionAbortedError:
            self.rollback_due_to_deadlock(txn_id)
            raise

    def _get_transaction(self, txn_id: TransactionID) -> Transaction:
        if txn_id not in self.transactions:
            raise KeyError(f"Unknown transaction {txn_id}.")
        return self.transactions[txn_id]

    def _require_active_transaction(self, txn_id: TransactionID) -> Transaction:
        txn = self._get_transaction(txn_id)
        if txn.state != TransactionState.ACTIVE:
            raise TransactionAbortedError(f"Transaction {txn_id} is {txn.state.value}.")
        return txn
