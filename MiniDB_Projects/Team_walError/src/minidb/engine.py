"""engine.py — the Database facade: the single public entrypoint.

This is the *end-to-end skeleton*. It defines the stable public surface
(`Database`, `Result`, `MiniDBError`) that demos, tests, and the CLI depend on.
Right now it wires a minimal pipeline so the whole system runs end-to-end from
day one; later modules (parser, planner, executor, storage) plug into the
clearly-marked seams below without changing this public API.

Pipeline (target):
    sql str -> sql.parse -> plan.optimize -> executor.run -> Result
Cross-cutting: transactions/locks wrap writes; WAL logs before write.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


class MiniDBError(Exception):
    """Base class for all MiniDB errors (parse, planning, execution, storage)."""


@dataclass
class Result:
    """The uniform result of executing one SQL statement.

    Attributes:
        columns: column names for a row-producing statement (e.g. SELECT).
        rows:    list of tuples, one per result row.
        rowcount: number of rows affected/returned (INSERT/DELETE report affected).
        message: human-readable status (e.g. "CREATE TABLE", "1 row inserted").
    """

    columns: list[str] = field(default_factory=list)
    rows: list[tuple[Any, ...]] = field(default_factory=list)
    rowcount: int = 0
    message: str = ""

    def __str__(self) -> str:  # pragma: no cover - presentation helper
        if not self.columns:
            return self.message or f"OK ({self.rowcount} rows)"
        lines = [" | ".join(self.columns), "-+-".join("-" * len(c) for c in self.columns)]
        for row in self.rows:
            lines.append(" | ".join("NULL" if v is None else str(v) for v in row))
        footer = f"({len(self.rows)} row{'s' if len(self.rows) != 1 else ''})"
        return "\n".join(lines + [footer])


class Database:
    """Facade over the whole engine.

    Open a database at `path` (use ":memory:" for a throwaway in-memory DB),
    then call `execute(sql)`. The constructor will, as modules come online,
    bring up: disk manager -> buffer pool -> catalog -> WAL recovery, plus the
    lock manager / transaction manager and the LSM engine (Track C).
    """

    def __init__(self, path: str = ":memory:") -> None:
        self.path = path
        self.in_memory = path == ":memory:"
        self._closed = False
        from .wal import WriteAheadLog
        from .transaction import TransactionManager

        # The WAL is the durable source of truth; it reads any existing records
        # so recovery can replay them. The heap file (.db) is rebuilt from it.
        self.wal = WriteAheadLog(path)
        self.txn_manager = TransactionManager()
        self.current_txn = None  # None => autocommit mode
        self.recovery_stats: dict = {}
        self._rebuild_state()  # truncate heap, build storage stack, replay WAL

    # -- storage stack lifecycle --------------------------------------------

    def _rebuild_state(self) -> None:
        """(Re)build catalog/storage from scratch and replay committed WAL.

        Used on open (recovery) and after ROLLBACK/deadlock to discard the
        in-memory effects of transactions that never committed.
        """
        import os
        from .disk_manager import DiskManager
        from .buffer_pool import BufferPool
        from .catalog import Catalog
        from .wal import replay

        if not self.in_memory and os.path.exists(self.path):
            open(self.path, "wb").close()  # heap is rebuilt from the WAL
        self.disk = DiskManager(self.path)
        self.buffer = BufferPool(self.disk)
        self.catalog = Catalog(self.buffer)
        self.recovery_stats = replay(self.wal.read_all(), self.catalog)

    def execute(self, sql: str) -> Result:
        """Parse and execute one SQL statement, returning a Result."""
        if self._closed:
            raise MiniDBError("database is closed")
        text = sql.strip().rstrip(";").strip()
        if not text:
            return Result(message="OK (empty statement)")
        if text.upper() == "PING":  # liveness probe used by the CLI/tests
            return Result(columns=["pong"], rows=[("pong",)], rowcount=1, message="PONG")
        try:
            return self._run(text)
        except MiniDBError:
            raise
        except (KeyError, ValueError, TypeError) as e:
            # translate expected user-input errors into MiniDBError for the CLI
            raise MiniDBError(str(e)) from e

    def _run(self, text: str) -> Result:
        from . import sql as ast

        stmt = ast.parse(text)
        if isinstance(stmt, ast.Begin):
            return self._do_begin()
        if isinstance(stmt, ast.Commit):
            return self._do_commit()
        if isinstance(stmt, ast.Rollback):
            return self._do_rollback()
        if isinstance(stmt, (ast.Select, ast.Explain)):
            return self._do_read(stmt)
        return self._do_write(stmt)

    # -- transaction control ------------------------------------------------

    def _do_begin(self) -> Result:
        if self.current_txn is not None:
            raise MiniDBError("already in a transaction")
        self.current_txn = self.txn_manager.begin()
        self.wal.append({"op": "begin", "txn": self.current_txn.txn_id})
        return Result(message="BEGIN")

    def _do_commit(self) -> Result:
        if self.current_txn is None:
            raise MiniDBError("no transaction in progress")
        self.wal.append({"op": "commit", "txn": self.current_txn.txn_id})
        self.wal.flush()  # durability point
        self.txn_manager.commit(self.current_txn)
        self.current_txn = None
        return Result(message="COMMIT")

    def _do_rollback(self) -> Result:
        if self.current_txn is None:
            return Result(message="ROLLBACK")
        self.wal.append({"op": "abort", "txn": self.current_txn.txn_id})
        self.wal.flush()
        self.txn_manager.abort(self.current_txn)
        self.current_txn = None
        self._rebuild_state()  # discard uncommitted in-memory effects
        return Result(message="ROLLBACK")

    def _handle_deadlock(self, txn) -> None:
        from .transaction import TxnState

        if txn.state is TxnState.ACTIVE:
            self.txn_manager.abort(txn)
        if txn is self.current_txn:
            self.current_txn = None
        self._rebuild_state()  # discard the victim's uncommitted effects

    # -- reads (shared locks) ----------------------------------------------

    def _do_read(self, stmt) -> Result:
        from . import sql as ast
        from . import executor as ex
        from . import plan
        from .lock_manager import DeadlockError
        from .transaction import TxnState

        select = stmt.query if isinstance(stmt, ast.Explain) else stmt
        tables = self._tables_of_select(select)
        txn = self.current_txn or self.txn_manager.begin()
        transient = self.current_txn is None
        try:
            for t in tables:
                txn.lock_shared(t)
            if isinstance(stmt, ast.Explain):
                tree = plan.optimize(select, self.catalog)
                ptext = plan.format_plan(tree)
                lines = ptext.split("\n")
                return Result(columns=["query plan"], rows=[(ln,) for ln in lines],
                              rowcount=len(lines))
            return ex.materialize(plan.optimize(select, self.catalog))
        except DeadlockError as e:
            self._handle_deadlock(txn)
            raise MiniDBError(str(e)) from e
        finally:
            if transient and txn.state is TxnState.ACTIVE:
                self.txn_manager.commit(txn)  # release shared locks

    def _tables_of_select(self, select) -> list[str]:
        names = [select.from_table] + [j.table for j in select.joins]
        seen, ordered = set(), []
        for n in names:                 # dedupe, preserve order
            if n not in seen:
                seen.add(n)
                ordered.append(n)
        return ordered

    # -- writes (exclusive locks + WAL) ------------------------------------

    def _do_write(self, stmt) -> Result:
        from .lock_manager import DeadlockError
        from .transaction import TxnState

        txn = self.current_txn or self.txn_manager.begin()
        autocommit = self.current_txn is None
        if autocommit:
            self.wal.append({"op": "begin", "txn": txn.txn_id})
        try:
            result = self._apply_write(stmt, txn)
            if autocommit:
                self.wal.append({"op": "commit", "txn": txn.txn_id})
                self.wal.flush()
                self.txn_manager.commit(txn)
            return result
        except DeadlockError as e:
            self._handle_deadlock(txn)
            raise MiniDBError(str(e)) from e
        except Exception:
            # statement failed: discard its partial in-memory effects so the
            # statement is atomic (the uncommitted WAL records are never replayed)
            if autocommit:
                if txn.state is TxnState.ACTIVE:
                    self.txn_manager.abort(txn)
                self._rebuild_state()
            raise

    def _apply_write(self, stmt, txn) -> Result:
        from . import sql as ast
        from . import executor as ex

        if isinstance(stmt, ast.CreateTable):
            txn.lock_exclusive(stmt.table)
            result = ex.exec_create_table(stmt, self.catalog)  # validates + creates
            self.wal.append({
                "op": "create_table", "txn": txn.txn_id, "table": stmt.table,
                "columns": [[c.name, c.type.value, c.nullable, c.primary_key]
                            for c in stmt.columns],
            })
            return result
        if isinstance(stmt, ast.CreateIndex):
            txn.lock_exclusive(stmt.table)
            result = ex.exec_create_index(stmt, self.catalog)
            self.wal.append({"op": "create_index", "txn": txn.txn_id,
                             "table": stmt.table, "column": stmt.column})
            return result
        if isinstance(stmt, ast.Insert):
            table = self.catalog.get_table(stmt.table)
            txn.lock_exclusive(stmt.table)
            count = 0
            for raw in stmt.rows:
                values = ex._row_in_schema_order(table, stmt.columns, raw)
                table.insert_row(values)              # apply (may raise on dup PK)
                self.wal.append({"op": "insert", "txn": txn.txn_id,
                                 "table": stmt.table, "values": list(values)})
                count += 1
            return Result(rowcount=count, message=f"{count} row(s) inserted")
        if isinstance(stmt, ast.Delete):
            table = self.catalog.get_table(stmt.table)
            txn.lock_exclusive(stmt.table)
            scan = ex.SeqScan(table, stmt.table)
            op = ex.Filter(scan, stmt.where) if stmt.where is not None else scan
            victims = [(r.rids[stmt.table], tuple(r.values)) for r in op]
            count = 0
            for rid, values in victims:
                if table.delete_by_rid(rid, values):
                    self.wal.append({"op": "delete", "txn": txn.txn_id,
                                     "table": stmt.table, "values": list(values)})
                    count += 1
            return Result(rowcount=count, message=f"{count} row(s) deleted")
        raise MiniDBError(f"unsupported statement: {type(stmt).__name__}")

    def close(self) -> None:
        """Flush and close the storage stack and the WAL."""
        if self._closed:
            return
        self.buffer.flush_all()
        self.disk.close()
        self.wal.close()
        self._closed = True

    def crash(self) -> None:
        """Simulate a crash: drop in-memory state WITHOUT flushing or committing.

        Used by the recovery demo/tests. The WAL file keeps whatever was already
        fsynced (i.e. committed transactions); everything else is lost.
        """
        self.disk.close()      # close fd; do NOT flush the buffer pool
        if self.wal._f is not None:
            self.wal._f.close()
            self.wal._f = None
        self._closed = True

    def __enter__(self) -> "Database":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()
