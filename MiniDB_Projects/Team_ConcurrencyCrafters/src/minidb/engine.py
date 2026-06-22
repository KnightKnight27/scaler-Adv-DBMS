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
    WALManager,
)
from .types import QueryPlan, RecordID, ResourceID, TableSchema, TransactionMode, Value


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
        self.catalog = Catalog(self.root_dir / "catalog.json")
        self.storage = HeapStorageEngine(self.root_dir, self.catalog)
        self.parser = SQLParser()
        self.optimizer = QueryOptimizer(self.catalog)
        self.transaction_manager = TransactionManager(
            wal_manager=wal_manager,
            recovery_manager=recovery_manager,
            mvcc_manager=mvcc_manager,
        )
        self.current_txn_id: int | None = None

    def execute(self, sql: str, txn_id: int | None = None) -> Any:
        statement = self.parser.parse(sql)
        active_txn_id = txn_id if txn_id is not None else self.current_txn_id
        return self._execute_statement(statement, active_txn_id)

    def explain(self, sql: str) -> str:
        statement = self.parser.parse(sql)
        if not isinstance(statement, SelectStatement):
            raise ValueError("EXPLAIN requires a SELECT statement.")
        plan = self.optimizer.optimize_select(statement)
        return plan.explain()

    def _execute_statement(self, statement: Statement, txn_id: int | None) -> Any:
        if isinstance(statement, BeginStatement):
            self.current_txn_id = self.transaction_manager.begin()
            return {"txn_id": self.current_txn_id}
        if isinstance(statement, CommitStatement):
            if self.current_txn_id is None:
                raise ValueError("No active transaction to commit.")
            self.transaction_manager.commit(self.current_txn_id)
            committed_txn = self.current_txn_id
            self.current_txn_id = None
            return {"txn_id": committed_txn, "status": "COMMITTED"}
        if isinstance(statement, RollbackStatement):
            if self.current_txn_id is None:
                raise ValueError("No active transaction to rollback.")
            self.transaction_manager.rollback(self.current_txn_id)
            aborted_txn = self.current_txn_id
            self.current_txn_id = None
            return {"txn_id": aborted_txn, "status": "ABORTED"}
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
            if statement.explain:
                return plan.explain()
            return self._execute_select(statement, plan, txn_id)
        if isinstance(statement, DeleteStatement):
            return self._execute_delete(statement, txn_id)
        raise ValueError(f"Unsupported statement type: {type(statement)!r}")

    def _execute_insert(self, statement: InsertStatement, txn_id: int | None) -> dict[str, Any]:
        schema = self.catalog.get_table(statement.table_name).schema
        if len(statement.values) != len(schema.columns):
            raise ValueError(
                f"INSERT expected {len(schema.columns)} values, got {len(statement.values)}."
            )
        row = {
            column.name: self._coerce_value(column.data_type, value)
            for column, value in zip(schema.columns, statement.values)
        }
        resource_id = self._resource_for_pending_insert(statement.table_name, row)
        self.transaction_manager.before_write(txn_id, resource_id)
        self.transaction_manager.wal_manager.log_write(
            txn_id or 0, resource_id, "INSERT"
        )
        rid = self.storage.insert(statement.table_name, row)
        self.transaction_manager.register_undo(
            txn_id,
            lambda: self.storage.delete(statement.table_name, rid),
        )
        return {"record_id": rid, "row": row}

    def _execute_select(
        self, statement: SelectStatement, plan: QueryPlan, txn_id: int | None
    ) -> list[dict[str, Value]] | int:
        if statement.join is not None:
            rows = self._execute_join(statement, plan, txn_id)
            return len(rows) if statement.count_star else rows

        if plan.operator == "INDEX_SCAN" and statement.predicate is not None:
            rids = self.storage.lookup_index(
                statement.table_name,
                statement.predicate.column,
                int(statement.predicate.value),
            )
            rows: list[dict[str, Value]] = []
            for rid in rids:
                resource_id = self._resource_for_rid(statement.table_name, rid)
                self.transaction_manager.before_read(txn_id, resource_id)
                row = self.storage.read(statement.table_name, rid)
                if row is not None and self._matches_predicate(row, statement.predicate):
                    rows.append(row)
            return len(rows) if statement.count_star else rows

        rows = []
        for rid in self.storage.scan_record_ids(statement.table_name):
            resource_id = self._resource_for_rid(statement.table_name, rid)
            self.transaction_manager.before_read(txn_id, resource_id)
            row = self.storage.read(statement.table_name, rid)
            if row is None:
                continue
            if statement.predicate is None or self._matches_predicate(row, statement.predicate):
                rows.append(row)
        return len(rows) if statement.count_star else rows

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
        outer_rows = self._scan_rows_with_lock(outer_table, txn_id)
        inner_rows = self._scan_rows_with_lock(inner_table, txn_id)
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
        deleted_rows = 0
        candidate_rids: list[RecordID]
        index = self.catalog.get_index_for_column(statement.table_name, statement.predicate.column)
        if index is not None and isinstance(statement.predicate.value, int):
            candidate_rids = self.storage.lookup_index(
                statement.table_name,
                statement.predicate.column,
                statement.predicate.value,
            )
        else:
            candidate_rids = self.storage.scan_record_ids(statement.table_name)
        for rid in candidate_rids:
            resource_id = self._resource_for_rid(statement.table_name, rid)
            self.transaction_manager.before_write(txn_id, resource_id)
            row = self.storage.read(statement.table_name, rid)
            if row is None or not self._matches_predicate(row, statement.predicate):
                continue
            self.transaction_manager.wal_manager.log_write(txn_id or 0, resource_id, "DELETE")
            deleted = self.storage.delete(statement.table_name, rid)
            if deleted is not None:
                deleted_rows += 1
                self.transaction_manager.register_undo(
                    txn_id,
                    lambda rid=rid, deleted=deleted: self.storage.restore(
                        statement.table_name, rid, deleted
                    ),
                )
        return {"deleted_rows": deleted_rows}

    def _scan_rows_with_lock(
        self, table_name: str, txn_id: int | None
    ) -> list[dict[str, Value]]:
        rows: list[dict[str, Value]] = []
        for rid in self.storage.scan_record_ids(table_name):
            resource_id = self._resource_for_rid(table_name, rid)
            self.transaction_manager.before_read(txn_id, resource_id)
            row = self.storage.read(table_name, rid)
            if row is not None:
                rows.append(row)
        return rows

    def _resource_for_pending_insert(
        self, table_name: str, row: dict[str, Value]
    ) -> ResourceID:
        primary_key = self.storage.primary_key_column(table_name)
        return f"{table_name}:pk:{row[primary_key]}"

    def _resource_for_rid(self, table_name: str, rid: RecordID) -> ResourceID:
        row = self.storage.read(table_name, rid)
        if row is not None:
            primary_key = self.storage.primary_key_column(table_name)
            return f"{table_name}:pk:{row[primary_key]}"
        return f"{table_name}:rid:{rid.page_id}:{rid.slot_id}"

    @staticmethod
    def _matches_predicate(row: dict[str, Value], predicate) -> bool:
        if predicate.operator != "=":
            raise ValueError(f"Unsupported predicate operator '{predicate.operator}'.")
        return row.get(predicate.column) == predicate.value

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

