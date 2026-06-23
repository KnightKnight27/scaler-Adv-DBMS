from __future__ import annotations

import json
import threading
import time
from collections import defaultdict
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING, Callable

from .types import LockType, ResourceID, TransactionID, TransactionMode, TransactionState, Value

if TYPE_CHECKING:
    from .catalog import Catalog
    from .storage import HeapStorageEngine


class DeadlockError(RuntimeError):
    pass


class TransactionAbortedError(RuntimeError):
    pass


@dataclass(slots=True)
class VersionedRecord:
    table_name: str
    key: int
    value: dict[str, Value] | None
    created_by_txn: TransactionID
    begin_ts: int | None = None
    deleted_by_txn: TransactionID | None = None
    end_ts: int | None = None
    committed: bool = False
    tombstone: bool = False
    operation: str = "INSERT"


class WALManager:
    def __init__(self, wal_path: str | Path):
        self.wal_path = Path(wal_path)
        self.wal_path.parent.mkdir(parents=True, exist_ok=True)
        if not self.wal_path.exists():
            self.wal_path.write_text("", encoding="utf-8")
        self._lock = threading.RLock()
        self.events: list[str] = []
        self._buffer: list[dict[str, object]] = []
        self._next_lsn = 1
        self._hydrate_counters()

    def begin_transaction(
        self, txn_id: TransactionID, mode: TransactionMode, snapshot_ts: int
    ) -> None:
        self._append_record(
            {
                "type": "BEGIN",
                "txn_id": txn_id,
                "mode": mode.value,
                "snapshot_ts": snapshot_ts,
            }
        )

    def log_insert(
        self,
        txn_id: TransactionID,
        table_name: str,
        key: int,
        row: dict[str, Value],
        mode: TransactionMode,
    ) -> None:
        self._append_record(
            {
                "type": "INSERT",
                "txn_id": txn_id,
                "table": table_name,
                "key": key,
                "row": row,
                "mode": mode.value,
            }
        )

    def log_delete(
        self,
        txn_id: TransactionID,
        table_name: str,
        key: int,
        old_row: dict[str, Value],
        mode: TransactionMode,
    ) -> None:
        self._append_record(
            {
                "type": "DELETE",
                "txn_id": txn_id,
                "table": table_name,
                "key": key,
                "old_row": old_row,
                "mode": mode.value,
            }
        )

    def log_write(self, txn_id: TransactionID, resource_id: ResourceID, action: str) -> None:
        self._append_record(
            {
                "type": action.upper(),
                "txn_id": txn_id,
                "resource": resource_id,
            }
        )

    def before_commit(self, txn_id: TransactionID, commit_ts: int | None = None) -> None:
        record: dict[str, object] = {"type": "COMMIT", "txn_id": txn_id}
        if commit_ts is not None:
            record["commit_ts"] = commit_ts
        self._append_record(record)
        self.flush()

    def on_abort(self, txn_id: TransactionID) -> None:
        self._append_record({"type": "ABORT", "txn_id": txn_id})
        self.flush()

    def before_page_flush(self, page_id: int) -> None:
        if self._buffer:
            self.flush()
        self.events.append(f"wal durable before page flush page={page_id}")

    def flush(self) -> None:
        with self._lock:
            if not self._buffer:
                return
            with self.wal_path.open("a", encoding="utf-8") as handle:
                for record in self._buffer:
                    handle.write(json.dumps(record, sort_keys=True) + "\n")
            last_lsn = int(self._buffer[-1]["lsn"])
            self.events.append(f"wal flush lsn={last_lsn}")
            self._buffer.clear()

    def read_records(self) -> list[dict[str, object]]:
        with self._lock:
            self.flush()
            content = self.wal_path.read_text(encoding="utf-8")
        records: list[dict[str, object]] = []
        for line in content.splitlines():
            stripped = line.strip()
            if stripped:
                records.append(json.loads(stripped))
        return records

    def max_transaction_id(self) -> int:
        transaction_ids = [
            int(record["txn_id"])
            for record in self.read_records()
            if "txn_id" in record
        ]
        return max(transaction_ids, default=0)

    def max_commit_ts(self) -> int:
        commit_timestamps = [
            int(record["commit_ts"])
            for record in self.read_records()
            if record.get("type") == "COMMIT" and "commit_ts" in record
        ]
        return max(commit_timestamps, default=0)

    def _append_record(self, record: dict[str, object]) -> None:
        with self._lock:
            payload = {"lsn": self._next_lsn, **record}
            self._next_lsn += 1
            self._buffer.append(payload)

    def _hydrate_counters(self) -> None:
        existing = self.wal_path.read_text(encoding="utf-8").splitlines()
        last_lsn = 0
        for line in existing:
            stripped = line.strip()
            if not stripped:
                continue
            record = json.loads(stripped)
            last_lsn = max(last_lsn, int(record.get("lsn", 0)))
        self._next_lsn = last_lsn + 1


class VersionStore:
    def __init__(self, store_path: str | Path):
        self.store_path = Path(store_path)
        self.store_path.parent.mkdir(parents=True, exist_ok=True)
        if not self.store_path.exists():
            self.store_path.write_text("{}", encoding="utf-8")
        self._lock = threading.RLock()
        self.committed: dict[str, dict[int, list[VersionedRecord]]] = {}
        self.pending: dict[TransactionID, list[VersionedRecord]] = defaultdict(list)
        self.logs: list[str] = []
        self._load()

    def stage_insert(
        self, txn_id: TransactionID, table_name: str, key: int, row: dict[str, Value]
    ) -> VersionedRecord:
        version = VersionedRecord(
            table_name=table_name,
            key=key,
            value=dict(row),
            created_by_txn=txn_id,
            committed=False,
            tombstone=False,
            operation="INSERT",
        )
        with self._lock:
            self.pending[txn_id].append(version)
        self.logs.append(f"version staged insert txn={txn_id} table={table_name} key={key}")
        return version

    def stage_delete(
        self, txn_id: TransactionID, table_name: str, key: int, row: dict[str, Value]
    ) -> VersionedRecord:
        version = VersionedRecord(
            table_name=table_name,
            key=key,
            value=dict(row),
            created_by_txn=txn_id,
            committed=False,
            tombstone=True,
            operation="DELETE",
        )
        with self._lock:
            self.pending[txn_id].append(version)
        self.logs.append(f"version staged delete txn={txn_id} table={table_name} key={key}")
        return version

    def commit(self, txn_id: TransactionID, commit_ts: int) -> None:
        with self._lock:
            staged = list(self.pending.pop(txn_id, []))
            for version in staged:
                table_versions = self.committed.setdefault(version.table_name, {})
                key_versions = table_versions.setdefault(version.key, [])
                if key_versions:
                    previous = key_versions[-1]
                    previous.end_ts = commit_ts
                    if version.tombstone:
                        previous.deleted_by_txn = txn_id
                version.committed = True
                version.begin_ts = commit_ts
                key_versions.append(version)
            self._save()
        self.logs.append(f"version commit txn={txn_id} commit_ts={commit_ts}")

    def rollback(self, txn_id: TransactionID) -> None:
        with self._lock:
            self.pending.pop(txn_id, None)
        self.logs.append(f"version rollback txn={txn_id}")

    def read_for_txn(
        self,
        table_name: str,
        key: int,
        *,
        txn_id: TransactionID | None,
        snapshot_ts: int | None,
        base_row: dict[str, Value] | None,
    ) -> dict[str, Value] | None:
        with self._lock:
            pending = [
                version
                for version in self.pending.get(txn_id or -1, [])
                if version.table_name == table_name and version.key == key
            ]
            if pending:
                latest_pending = pending[-1]
                return None if latest_pending.tombstone else dict(latest_pending.value or {})

            committed_versions = list(self.committed.get(table_name, {}).get(key, []))
            if snapshot_ts is None:
                visible = committed_versions[-1] if committed_versions else None
            else:
                visible = None
                for version in committed_versions:
                    if version.begin_ts is not None and version.begin_ts <= snapshot_ts:
                        visible = version
            if visible is not None:
                return None if visible.tombstone else dict(visible.value or {})
            if self.has_pending_other_txn(table_name, key, txn_id):
                return None
            return dict(base_row) if base_row is not None else None

    def latest_committed_row(
        self, table_name: str, key: int, base_row: dict[str, Value] | None
    ) -> dict[str, Value] | None:
        return self.read_for_txn(
            table_name,
            key,
            txn_id=None,
            snapshot_ts=None,
            base_row=base_row,
        )

    def has_pending_other_txn(
        self, table_name: str, key: int, txn_id: TransactionID | None
    ) -> bool:
        for pending_txn_id, staged in self.pending.items():
            if txn_id is not None and pending_txn_id == txn_id:
                continue
            if any(version.table_name == table_name and version.key == key for version in staged):
                return True
        return False

    def logical_keys(
        self, table_name: str, *, txn_id: TransactionID | None = None
    ) -> set[int]:
        keys = set(self.committed.get(table_name, {}).keys())
        if txn_id is not None:
            keys.update(
                version.key
                for version in self.pending.get(txn_id, [])
                if version.table_name == table_name
            )
        return keys

    def current_rows(self, table_name: str) -> list[dict[str, Value]]:
        rows: list[dict[str, Value]] = []
        for key in sorted(self.committed.get(table_name, {})):
            versions = self.committed[table_name][key]
            if not versions:
                continue
            latest = versions[-1]
            if latest.tombstone:
                continue
            rows.append(dict(latest.value or {}))
        return rows

    def version_count(self, table_name: str | None = None) -> int:
        tables = [table_name] if table_name is not None else list(self.committed.keys())
        return sum(
            len(self.committed.get(table, {}).get(key, []))
            for table in tables
            for key in self.committed.get(table, {})
        )

    def storage_overhead_bytes(self) -> int:
        if not self.store_path.exists():
            return 0
        return self.store_path.stat().st_size

    def max_commit_ts(self) -> int:
        max_commit = 0
        for table_versions in self.committed.values():
            for versions in table_versions.values():
                for version in versions:
                    if version.begin_ts is not None:
                        max_commit = max(max_commit, version.begin_ts)
        return max_commit

    def replace_committed_versions(
        self, committed_versions: dict[str, dict[int, list[VersionedRecord]]]
    ) -> None:
        with self._lock:
            self.committed = committed_versions
            self.pending = defaultdict(list)
            self._save()
        self.logs.append("version store rebuilt from WAL recovery")

    def _save(self) -> None:
        payload: dict[str, dict[str, list[dict[str, object]]]] = {}
        for table_name, table_versions in self.committed.items():
            payload[table_name] = {}
            for key, versions in table_versions.items():
                payload[table_name][str(key)] = [asdict(version) for version in versions]
        self.store_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    def _load(self) -> None:
        content = self.store_path.read_text(encoding="utf-8").strip()
        if not content:
            self.committed = {}
            return
        raw = json.loads(content)
        self.committed = {}
        for table_name, table_versions in raw.items():
            self.committed[table_name] = {}
            for key, versions in table_versions.items():
                self.committed[table_name][int(key)] = [
                    VersionedRecord(**version) for version in versions
                ]


class MVCCManager:
    def __init__(self, version_store: VersionStore):
        self.version_store = version_store
        self.lock_manager: LockManager | None = None
        self.logs: list[str] = []

    def bind_lock_manager(self, lock_manager: "LockManager") -> None:
        self.lock_manager = lock_manager

    def begin(self, txn_id: TransactionID, snapshot_ts: int) -> None:
        self.logs.append(f"mvcc begin txn={txn_id} snapshot_ts={snapshot_ts}")

    def before_read(self, txn_id: TransactionID, resource_id: ResourceID) -> None:
        self.logs.append(f"mvcc snapshot read txn={txn_id} resource={resource_id}")

    def before_write(self, txn_id: TransactionID, resource_id: ResourceID) -> None:
        if self.lock_manager is not None:
            self.lock_manager.acquire(txn_id, resource_id, LockType.EXCLUSIVE)
        self.logs.append(f"mvcc write intent txn={txn_id} resource={resource_id}")

    def stage_insert(
        self, txn_id: TransactionID, table_name: str, key: int, row: dict[str, Value]
    ) -> None:
        self.version_store.stage_insert(txn_id, table_name, key, row)

    def stage_delete(
        self, txn_id: TransactionID, table_name: str, key: int, row: dict[str, Value]
    ) -> None:
        self.version_store.stage_delete(txn_id, table_name, key, row)

    def commit(self, txn_id: TransactionID, commit_ts: int) -> None:
        self.version_store.commit(txn_id, commit_ts)
        self.logs.append(f"mvcc commit txn={txn_id} commit_ts={commit_ts}")

    def rollback(self, txn_id: TransactionID) -> None:
        self.version_store.rollback(txn_id)
        self.logs.append(f"mvcc rollback txn={txn_id}")

    def read_visible_row(
        self,
        table_name: str,
        key: int,
        *,
        txn_id: TransactionID | None,
        snapshot_ts: int | None,
        base_row: dict[str, Value] | None,
    ) -> dict[str, Value] | None:
        return self.version_store.read_for_txn(
            table_name,
            key,
            txn_id=txn_id,
            snapshot_ts=snapshot_ts,
            base_row=base_row,
        )

    def current_committed_row(
        self, table_name: str, key: int, base_row: dict[str, Value] | None
    ) -> dict[str, Value] | None:
        return self.version_store.latest_committed_row(table_name, key, base_row)

    def logical_keys(self, table_name: str, *, txn_id: TransactionID | None = None) -> set[int]:
        return self.version_store.logical_keys(table_name, txn_id=txn_id)

    def current_rows(self, table_name: str) -> list[dict[str, Value]]:
        return self.version_store.current_rows(table_name)

    def version_count(self, table_name: str | None = None) -> int:
        return self.version_store.version_count(table_name)

    def storage_overhead_bytes(self) -> int:
        return self.version_store.storage_overhead_bytes()

    def max_commit_ts(self) -> int:
        return self.version_store.max_commit_ts()


class RecoveryManager:
    def __init__(self):
        self.storage: HeapStorageEngine | None = None
        self.catalog: Catalog | None = None
        self.wal_manager: WALManager | None = None
        self.mvcc_manager: MVCCManager | None = None
        self.logs: list[str] = []

    def bind(
        self,
        *,
        storage: "HeapStorageEngine",
        catalog: "Catalog",
        wal_manager: WALManager,
        mvcc_manager: MVCCManager,
    ) -> None:
        self.storage = storage
        self.catalog = catalog
        self.wal_manager = wal_manager
        self.mvcc_manager = mvcc_manager

    def rollback(self, txn_id: TransactionID) -> None:
        self.logs.append(f"recovery rollback hook txn={txn_id}")

    def recover(self) -> dict[str, object]:
        if (
            self.storage is None
            or self.catalog is None
            or self.wal_manager is None
            or self.mvcc_manager is None
        ):
            return {"status": "UNBOUND"}

        records = self.wal_manager.read_records()
        if not records:
            return {"status": "NO_WAL"}

        committed: dict[int, int] = {}
        aborted: set[int] = set()
        operations: list[dict[str, object]] = []
        for record in records:
            record_type = str(record.get("type", ""))
            txn_id = int(record.get("txn_id", 0)) if "txn_id" in record else 0
            if record_type == "COMMIT":
                committed[txn_id] = int(record.get("commit_ts", 0))
            elif record_type == "ABORT":
                aborted.add(txn_id)
            elif record_type in {"INSERT", "DELETE"}:
                operations.append(record)

        rebuilt_versions: dict[str, dict[int, list[VersionedRecord]]] = {}
        for record in operations:
            txn_id = int(record["txn_id"])
            if txn_id not in committed or txn_id in aborted:
                continue
            table_name = str(record["table"])
            key = int(record["key"])
            commit_ts = committed[txn_id]
            if str(record["type"]) == "INSERT":
                row = dict(record["row"])
                version = VersionedRecord(
                    table_name=table_name,
                    key=key,
                    value=row,
                    created_by_txn=txn_id,
                    begin_ts=commit_ts,
                    committed=True,
                    tombstone=False,
                    operation="INSERT",
                )
            else:
                old_row = dict(record["old_row"])
                version = VersionedRecord(
                    table_name=table_name,
                    key=key,
                    value=old_row,
                    created_by_txn=txn_id,
                    begin_ts=commit_ts,
                    committed=True,
                    tombstone=True,
                    operation="DELETE",
                )
            table_versions = rebuilt_versions.setdefault(table_name, {})
            key_versions = table_versions.setdefault(key, [])
            if key_versions:
                previous = key_versions[-1]
                previous.end_ts = commit_ts
                if version.tombstone:
                    previous.deleted_by_txn = txn_id
            key_versions.append(version)

        self.mvcc_manager.version_store.replace_committed_versions(rebuilt_versions)
        for table_name in self.catalog.tables:
            rows = self.mvcc_manager.current_rows(table_name)
            self.storage.rebuild_table(table_name, rows)

        summary = {
            "status": "RECOVERED",
            "committed_transactions": sorted(committed),
            "uncommitted_transactions": sorted(
                txn_id
                for txn_id in {
                    int(record["txn_id"]) for record in records if "txn_id" in record
                }
                if txn_id not in committed
            ),
        }
        self.logs.append(
            "recovery rebuilt state "
            f"committed={summary['committed_transactions']} "
            f"uncommitted={summary['uncommitted_transactions']}"
        )
        return summary


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
                    self.logs.append(f"deadlock detected cycle={cycle} victim={victim}")
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
    snapshot_ts: int = 0
    commit_ts: int | None = None
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
        runtime_dir = Path.cwd() / ".txn_runtime"
        runtime_dir.mkdir(parents=True, exist_ok=True)
        self.wal_manager = wal_manager or WALManager(runtime_dir / "transaction_manager.wal")
        self.recovery_manager = recovery_manager or RecoveryManager()
        self.mvcc_manager = mvcc_manager or MVCCManager(
            VersionStore(runtime_dir / "transaction_manager_versions.json")
        )
        self.mvcc_manager.bind_lock_manager(self.lock_manager)
        self.default_mode = default_mode
        self.transactions: dict[TransactionID, Transaction] = {}
        self.logs: list[str] = []
        self._next_txn_id = max(self.wal_manager.max_transaction_id() + 1, 1)
        self._commit_counter = max(
            self.mvcc_manager.max_commit_ts(),
            self.wal_manager.max_commit_ts(),
        )

    def begin(self) -> TransactionID:
        txn_id = self._next_txn_id
        self._next_txn_id += 1
        snapshot_ts = self._commit_counter
        transaction = Transaction(
            txn_id=txn_id,
            mode=self.default_mode,
            snapshot_ts=snapshot_ts,
        )
        self.transactions[txn_id] = transaction
        self.wal_manager.begin_transaction(txn_id, self.default_mode, snapshot_ts)
        if self.default_mode == TransactionMode.MVCC:
            self.mvcc_manager.begin(txn_id, snapshot_ts)
        self.logs.append(
            f"transaction begun txn={txn_id} mode={self.default_mode.value} snapshot={snapshot_ts}"
        )
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
            try:
                self.mvcc_manager.before_write(txn_id, resource_id)
            except DeadlockError:
                self.rollback_due_to_deadlock(txn_id)
                raise
            except TransactionAbortedError:
                self.rollback_due_to_deadlock(txn_id)
                raise
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
        self._commit_counter += 1
        txn.commit_ts = self._commit_counter
        if txn.mode == TransactionMode.MVCC:
            self.mvcc_manager.commit(txn_id, txn.commit_ts)
        else:
            self.mvcc_manager.commit(txn_id, txn.commit_ts)
        self.wal_manager.before_commit(txn_id, txn.commit_ts)
        txn.undo_actions.clear()
        self.lock_manager.release_transaction(txn_id)
        txn.state = TransactionState.COMMITTED
        self.logs.append(f"transaction committed txn={txn_id} commit_ts={txn.commit_ts}")

    def rollback(self, txn_id: TransactionID) -> None:
        txn = self._get_transaction(txn_id)
        if txn.state != TransactionState.ACTIVE:
            return
        for undo_action in reversed(txn.undo_actions):
            undo_action()
        txn.undo_actions.clear()
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

    def get_mode(self, txn_id: TransactionID | None = None) -> TransactionMode:
        if txn_id is None:
            return self.default_mode
        return self._get_transaction(txn_id).mode

    def get_snapshot_ts(self, txn_id: TransactionID | None) -> int | None:
        if txn_id is None:
            return None
        return self._get_transaction(txn_id).snapshot_ts

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
