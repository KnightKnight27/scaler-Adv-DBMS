from __future__ import annotations

from pathlib import Path
from typing import Any

from .catalog import Catalog
from .optimizer import QueryOptimizer
from .parser import (
    BeginStatement,
    CommitStatement,
    CreateIndexStatement,
    CreateTableStatement,
    DeleteStatement,
    InsertStatement,
    RollbackStatement,
    SQLParser,
    SelectStatement,
    SetModeStatement,
    Statement,
)
from .storage import HeapStorageEngine
from .transactions import (
    MVCCManager,
    RecoveryManager,
    TransactionAbortedError,
    TransactionManager,
    VersionStore,
    WALManager,
)
from .types import Predicate, QueryPlan, RecordID, ResourceID, TableSchema, TransactionMode, Value


class MiniDBEngine:
    def __init__(
        self,
        base_dir: str | Path,
        *,
        wal_manager: WALManager | None = None,
        recovery_manager: RecoveryManager | None = None,
        mvcc_manager: MVCCManager | None = None,
    ):
        self.root_dir = Path(base_dir)
        self.root_dir.mkdir(parents=True, exist_ok=True)
        self.wal_manager = wal_manager or WALManager(self.root_dir / "db.wal")
        self.mvcc_manager = mvcc_manager or MVCCManager(
            VersionStore(self.root_dir / "mvcc_versions.json")
        )
        self.recovery_manager = recovery_manager or RecoveryManager()
        self.catalog = Catalog(self.root_dir / "catalog.json")
        self.storage = HeapStorageEngine(
            self.root_dir,
            self.catalog,
            wal_manager=self.wal_manager,
        )
        self.parser = SQLParser()
        self.optimizer = QueryOptimizer(self.catalog)
        self.transaction_manager = TransactionManager(
            wal_manager=self.wal_manager,
            recovery_manager=self.recovery_manager,
            mvcc_manager=self.mvcc_manager,
        )
        self.recovery_manager.bind(
            storage=self.storage,
            catalog=self.catalog,
            wal_manager=self.wal_manager,
            mvcc_manager=self.mvcc_manager,
        )
        self.recovery_manager.recover()
        self.current_txn_id: int | None = None

    def begin_transaction(self, *, bind_to_session: bool = False) -> int:
        txn_id = self.transaction_manager.begin()
        if bind_to_session:
            self.current_txn_id = txn_id
        return txn_id

    def commit_transaction(self, txn_id: int | None = None) -> dict[str, Any]:
        target_txn_id = txn_id if txn_id is not None else self.current_txn_id
        if target_txn_id is None:
            raise ValueError("No active transaction to commit.")
        self.transaction_manager.commit(target_txn_id)
        if self.current_txn_id == target_txn_id:
            self.current_txn_id = None
        return {"txn_id": target_txn_id, "status": "COMMITTED"}

    def rollback_transaction(self, txn_id: int | None = None) -> dict[str, Any]:
        target_txn_id = txn_id if txn_id is not None else self.current_txn_id
        if target_txn_id is None:
            raise ValueError("No active transaction to rollback.")
        self.transaction_manager.rollback(target_txn_id)
        if self.current_txn_id == target_txn_id:
            self.current_txn_id = None
        return {"txn_id": target_txn_id, "status": "ABORTED"}

    def recover(self) -> dict[str, object]:
        return self.recovery_manager.recover()

    def vacuum(self) -> dict[str, object]:
        active_snapshots = self.transaction_manager.active_snapshot_timestamps()
        return self.mvcc_manager.vacuum(active_snapshots)

    def execute(self, sql: str, txn_id: int | None = None) -> Any:
        statement = self.parser.parse(sql)
        active_txn_id = txn_id if txn_id is not None else self.current_txn_id
        if active_txn_id is None and self._requires_transaction(statement):
            auto_txn_id = self.transaction_manager.begin()
            try:
                result = self._execute_statement(statement, auto_txn_id)
                self.transaction_manager.commit(auto_txn_id)
                return result
            except Exception:
                self.transaction_manager.rollback(auto_txn_id)
                raise
        return self._execute_statement(statement, active_txn_id)

    def explain(self, sql: str, txn_id: int | None = None) -> str:
        statement = self.parser.parse(sql)
        if not isinstance(statement, SelectStatement):
            raise ValueError("EXPLAIN requires a SELECT statement.")
        plan = self.optimizer.optimize_select(statement)
        self._annotate_plan_with_mode(plan, txn_id)
        return plan.explain()

    def _execute_statement(self, statement: Statement, txn_id: int | None) -> Any:
        if isinstance(statement, BeginStatement):
            return {"txn_id": self.begin_transaction(bind_to_session=True)}
        if isinstance(statement, CommitStatement):
            return self.commit_transaction(txn_id)
        if isinstance(statement, RollbackStatement):
            return self.rollback_transaction(txn_id)
        if isinstance(statement, SetModeStatement):
            self.transaction_manager.set_mode(statement.mode)
            return {"mode": statement.mode.value}
        if isinstance(statement, CreateTableStatement):
            schema = TableSchema(name=statement.table_name, columns=statement.columns)
            self.storage.create_table(schema)
            return {"created_table": statement.table_name}
        if isinstance(statement, CreateIndexStatement):
            self.storage.create_index(statement.table_name, statement.index_name, statement.column_name)
            return {"created_index": statement.index_name}
        if isinstance(statement, InsertStatement):
            return self._execute_insert(statement, txn_id)
        if isinstance(statement, SelectStatement):
            plan = self.optimizer.optimize_select(statement)
            self._annotate_plan_with_mode(plan, txn_id)
            if statement.explain:
                return plan.explain()
            return self._execute_select(statement, plan, txn_id)
        if isinstance(statement, DeleteStatement):
            return self._execute_delete(statement, txn_id)
        raise ValueError(f"Unsupported statement type: {type(statement)!r}")

    def _execute_insert(self, statement: InsertStatement, txn_id: int | None) -> dict[str, Any]:
        if txn_id is None:
            raise ValueError("INSERT requires a transaction context.")
        schema = self.catalog.get_table(statement.table_name).schema
        if len(statement.values) != len(schema.columns):
            raise ValueError(
                f"INSERT expected {len(schema.columns)} values, got {len(statement.values)}."
            )
        row = {
            column.name: self._coerce_value(column.data_type, value)
            for column, value in zip(schema.columns, statement.values)
        }
        primary_key = self.storage.primary_key_column(statement.table_name)
        key = int(row[primary_key])
        resource_id = self._resource_for_key(statement.table_name, key)
        self.transaction_manager.before_write(txn_id, resource_id)

        existing_row = self._logical_row_for_key(statement.table_name, key, txn_id)
        if existing_row is not None:
            raise ValueError(f"Logical primary key '{key}' already exists in '{statement.table_name}'.")

        txn_mode = self.transaction_manager.get_mode(txn_id)
        self.wal_manager.log_insert(txn_id, statement.table_name, key, row, txn_mode)

        anchor_rid = self._anchor_rid_for_key(statement.table_name, key)
        if anchor_rid is None:
            anchor_rid = self.storage.insert(statement.table_name, row)
            self.transaction_manager.register_undo(
                txn_id,
                lambda rid=anchor_rid, table_name=statement.table_name: self.storage.delete(
                    table_name, rid
                ),
            )
        self.mvcc_manager.stage_insert(txn_id, statement.table_name, key, row)
        return {"record_id": anchor_rid, "row": row}

    def _execute_select(
        self, statement: SelectStatement, plan: QueryPlan, txn_id: int | None
    ) -> list[dict[str, Value]]:
        predicates = self._select_predicates(statement)
        if statement.join is not None:
            rows = self._execute_join(statement, plan, txn_id)
            return self._format_select_result(rows, statement.count_star)

        rows: list[dict[str, Value]]
        primary_key_predicate = self._primary_key_equality_predicate(
            statement.table_name,
            predicates,
        )
        range_bounds = self._primary_key_range_bounds(statement.table_name, predicates)
        indexed_predicate = self._indexed_equality_predicate(statement.table_name, predicates)
        if (
            plan.operator == "INDEX_SCAN"
            and primary_key_predicate is not None
            and isinstance(primary_key_predicate.value, int)
        ):
            key = int(primary_key_predicate.value)
            row = self._read_logical_row_by_primary_key(statement.table_name, key, txn_id)
            rows = [row] if row is not None and self._matches_predicates(row, predicates) else []
        elif plan.operator == "INDEX_RANGE_SCAN" and range_bounds is not None:
            lower_bound, upper_bound = range_bounds
            rows = self._execute_primary_key_range_scan(
                statement.table_name,
                predicates,
                txn_id,
                start_key=lower_bound,
                end_key=upper_bound,
            )
        elif (
            plan.operator == "INDEX_SCAN"
            and indexed_predicate is not None
            and self.transaction_manager.get_mode(txn_id) == TransactionMode.TWO_PL
        ):
            rows = self._execute_secondary_index_scan(
                statement.table_name,
                indexed_predicate,
                predicates,
                txn_id,
            )
        else:
            rows = self._scan_logical_rows(statement.table_name, txn_id)
            if predicates:
                rows = [
                    row for row in rows if self._matches_predicates(row, predicates)
                ]
        return self._format_select_result(rows, statement.count_star)

    def _execute_secondary_index_scan(
        self,
        table_name: str,
        index_predicate: Predicate,
        predicates: list[Predicate],
        txn_id: int | None,
    ) -> list[dict[str, Value]]:
        candidate_rids = self.storage.lookup_index(
            table_name,
            index_predicate.column,
            int(index_predicate.value),
        )
        rows: list[dict[str, Value]] = []
        seen_keys: set[int] = set()
        for rid in candidate_rids:
            physical_row = self.storage.read(table_name, rid)
            if physical_row is None:
                continue
            primary_key = self.storage.primary_key_column(table_name)
            key = int(physical_row[primary_key])
            if key in seen_keys:
                continue
            resource_id = self._resource_for_key(table_name, key)
            self.transaction_manager.before_read(txn_id, resource_id)
            logical_row = self._logical_row_for_key(
                table_name,
                key,
                txn_id,
                base_row=physical_row,
            )
            if logical_row is not None and self._matches_predicates(logical_row, predicates):
                rows.append(logical_row)
                seen_keys.add(key)
        return rows

    def _execute_primary_key_range_scan(
        self,
        table_name: str,
        predicates: list[Predicate],
        txn_id: int | None,
        *,
        start_key: int | None,
        end_key: int | None,
    ) -> list[dict[str, Value]]:
        primary_key = self.storage.primary_key_column(table_name)
        candidate_rids = self.storage.lookup_index_range(
            table_name,
            primary_key,
            start_key=start_key,
            end_key=end_key,
        )
        rows: list[dict[str, Value]] = []
        seen_keys: set[int] = set()
        for rid in candidate_rids:
            physical_row = self.storage.read(table_name, rid)
            if physical_row is None:
                continue
            key = int(physical_row[primary_key])
            if key in seen_keys:
                continue
            resource_id = self._resource_for_key(table_name, key)
            self.transaction_manager.before_read(txn_id, resource_id)
            logical_row = self._logical_row_for_key(
                table_name,
                key,
                txn_id,
                base_row=physical_row,
            )
            if logical_row is not None and self._matches_predicates(logical_row, predicates):
                rows.append(logical_row)
                seen_keys.add(key)
        return rows

    def _execute_join(
        self, statement: SelectStatement, plan: QueryPlan, txn_id: int | None
    ) -> list[dict[str, Value]]:
        assert statement.join is not None
        outer_table = plan.details.get("outer", statement.join.left_table)
        inner_table = plan.details.get("inner", statement.join.right_table)
        if outer_table == statement.join.left_table:
            outer_column = statement.join.left_column
            inner_column = statement.join.right_column
        else:
            outer_column = statement.join.right_column
            inner_column = statement.join.left_column
        outer_rows = self._scan_logical_rows(outer_table, txn_id)
        inner_rows = self._scan_logical_rows(inner_table, txn_id)
        joined_rows: list[dict[str, Value]] = []
        for outer in outer_rows:
            for inner in inner_rows:
                if outer[outer_column] == inner[inner_column]:
                    merged = {
                        f"{outer_table}.{column}": value
                        for column, value in outer.items()
                    }
                    merged.update(
                        {
                            f"{inner_table}.{column}": value
                            for column, value in inner.items()
                        }
                    )
                    joined_rows.append(merged)
        return joined_rows

    def _execute_delete(
        self, statement: DeleteStatement, txn_id: int | None
    ) -> dict[str, Any]:
        if txn_id is None:
            raise ValueError("DELETE requires a transaction context.")
        deleted_rows = 0
        primary_key = self.storage.primary_key_column(statement.table_name)
        txn_mode = self.transaction_manager.get_mode(txn_id)

        if (
            statement.predicate.column == primary_key
            and isinstance(statement.predicate.value, int)
        ):
            candidate_keys = [int(statement.predicate.value)]
        else:
            candidate_keys = [int(row[primary_key]) for row in self._scan_logical_rows(statement.table_name, txn_id)]

        seen_keys: set[int] = set()
        for key in candidate_keys:
            if key in seen_keys:
                continue
            seen_keys.add(key)
            resource_id = self._resource_for_key(statement.table_name, key)
            self.transaction_manager.before_write(txn_id, resource_id)
            row = self._logical_row_for_key(statement.table_name, key, txn_id)
            if row is None or not self._matches_predicate(row, statement.predicate):
                continue
            self.wal_manager.log_delete(txn_id, statement.table_name, key, row, txn_mode)
            self.mvcc_manager.stage_delete(txn_id, statement.table_name, key, row)
            if txn_mode == TransactionMode.TWO_PL:
                anchor_rid = self._anchor_rid_for_key(statement.table_name, key)
                if anchor_rid is not None:
                    deleted = self.storage.delete(statement.table_name, anchor_rid)
                    if deleted is not None:
                        self.transaction_manager.register_undo(
                            txn_id,
                            lambda table_name=statement.table_name, rid=anchor_rid, deleted=deleted: self.storage.restore(
                                table_name, rid, deleted
                            ),
                        )
            deleted_rows += 1
        return {"deleted_rows": deleted_rows}

    def _scan_logical_rows(
        self, table_name: str, txn_id: int | None
    ) -> list[dict[str, Value]]:
        primary_key = self.storage.primary_key_column(table_name)
        physical_rows = {
            int(row[primary_key]): row
            for _, row in self.storage.scan(table_name)
        }
        all_keys = set(physical_rows) | self.mvcc_manager.logical_keys(table_name, txn_id=txn_id)
        rows: list[dict[str, Value]] = []
        for key in sorted(all_keys):
            resource_id = self._resource_for_key(table_name, key)
            self.transaction_manager.before_read(txn_id, resource_id)
            row = self._logical_row_for_key(
                table_name,
                key,
                txn_id,
                base_row=physical_rows.get(key),
            )
            if row is not None:
                rows.append(row)
        return rows

    def _read_logical_row_by_primary_key(
        self, table_name: str, key: int, txn_id: int | None
    ) -> dict[str, Value] | None:
        resource_id = self._resource_for_key(table_name, key)
        self.transaction_manager.before_read(txn_id, resource_id)
        anchor_rid = self._anchor_rid_for_key(table_name, key)
        base_row = self.storage.read(table_name, anchor_rid) if anchor_rid is not None else None
        return self._logical_row_for_key(table_name, key, txn_id, base_row=base_row)

    def _logical_row_for_key(
        self,
        table_name: str,
        key: int,
        txn_id: int | None,
        *,
        base_row: dict[str, Value] | None = None,
    ) -> dict[str, Value] | None:
        if base_row is None:
            anchor_rid = self._anchor_rid_for_key(table_name, key)
            base_row = self.storage.read(table_name, anchor_rid) if anchor_rid is not None else None
        mode = self.transaction_manager.get_mode(txn_id)
        snapshot_ts = (
            self.transaction_manager.get_snapshot_ts(txn_id)
            if mode == TransactionMode.MVCC
            else None
        )
        return self.mvcc_manager.read_visible_row(
            table_name,
            key,
            txn_id=txn_id,
            snapshot_ts=snapshot_ts,
            base_row=base_row,
        )

    def _anchor_rid_for_key(self, table_name: str, key: int) -> RecordID | None:
        primary_key = self.storage.primary_key_column(table_name)
        matches = self.storage.lookup_index(table_name, primary_key, key)
        return matches[0] if matches else None

    def _annotate_plan_with_mode(self, plan: QueryPlan, txn_id: int | None) -> None:
        mode = self.transaction_manager.get_mode(txn_id)
        plan.details["transaction_mode"] = (
            "MVCC_SNAPSHOT" if mode == TransactionMode.MVCC else "2PL_LOCKING"
        )
        for child in plan.children:
            child.details["transaction_mode"] = plan.details["transaction_mode"]

    def _requires_transaction(self, statement: Statement) -> bool:
        return isinstance(statement, (InsertStatement, DeleteStatement))

    def _resource_for_key(self, table_name: str, key: int) -> ResourceID:
        return f"{table_name}:pk:{key}"

    @staticmethod
    def _matches_predicate(row: dict[str, Value], predicate: Predicate) -> bool:
        row_value = row.get(predicate.column)
        if predicate.operator == "=":
            return row_value == predicate.value
        if row_value is None or not isinstance(predicate.value, int):
            return False
        if not isinstance(row_value, int):
            return False
        if predicate.operator == ">=":
            return row_value >= predicate.value
        if predicate.operator == "<=":
            return row_value <= predicate.value
        if predicate.operator == ">":
            return row_value > predicate.value
        if predicate.operator == "<":
            return row_value < predicate.value
        raise ValueError(f"Unsupported predicate operator '{predicate.operator}'.")

    @classmethod
    def _matches_predicates(cls, row: dict[str, Value], predicates: list[Predicate]) -> bool:
        return all(cls._matches_predicate(row, predicate) for predicate in predicates)

    @staticmethod
    def _format_select_result(
        rows: list[dict[str, Value]],
        count_star: bool,
    ) -> list[dict[str, Value]]:
        if count_star:
            return [{"count": len(rows)}]
        return rows

    @staticmethod
    def _select_predicates(statement: SelectStatement) -> list[Predicate]:
        if statement.predicates:
            return list(statement.predicates)
        return [statement.predicate] if statement.predicate is not None else []

    def _primary_key_equality_predicate(
        self,
        table_name: str,
        predicates: list[Predicate],
    ) -> Predicate | None:
        primary_key = self.storage.primary_key_column(table_name)
        for predicate in predicates:
            if predicate.column == primary_key and predicate.operator == "=":
                return predicate
        return None

    def _indexed_equality_predicate(
        self,
        table_name: str,
        predicates: list[Predicate],
    ) -> Predicate | None:
        for predicate in predicates:
            if predicate.operator != "=":
                continue
            if self.catalog.get_index_for_column(table_name, predicate.column) is not None:
                return predicate
        return None

    def _primary_key_range_bounds(
        self,
        table_name: str,
        predicates: list[Predicate],
    ) -> tuple[int | None, int | None] | None:
        primary_key = self.storage.primary_key_column(table_name)
        lower_bound: int | None = None
        upper_bound: int | None = None
        saw_range_predicate = False
        for predicate in predicates:
            if predicate.column != primary_key or not isinstance(predicate.value, int):
                continue
            if predicate.operator == ">=":
                lower_bound = max(lower_bound, int(predicate.value)) if lower_bound is not None else int(predicate.value)
                saw_range_predicate = True
            elif predicate.operator == ">":
                candidate = int(predicate.value) + 1
                lower_bound = max(lower_bound, candidate) if lower_bound is not None else candidate
                saw_range_predicate = True
            elif predicate.operator == "<=":
                upper_bound = min(upper_bound, int(predicate.value)) if upper_bound is not None else int(predicate.value)
                saw_range_predicate = True
            elif predicate.operator == "<":
                candidate = int(predicate.value) - 1
                upper_bound = min(upper_bound, candidate) if upper_bound is not None else candidate
                saw_range_predicate = True
        if not saw_range_predicate:
            return None
        if lower_bound is not None and upper_bound is not None and lower_bound > upper_bound:
            return (1, 0)
        return lower_bound, upper_bound

    @staticmethod
    def _coerce_value(data_type: str, value: Value) -> Value:
        normalized = data_type.upper()
        if normalized == "INT":
            if value is None:
                return None
            return int(value)
        if normalized == "TEXT":
            if value is None:
                return None
            return str(value)
        raise ValueError(f"Unsupported column type '{data_type}'.")
