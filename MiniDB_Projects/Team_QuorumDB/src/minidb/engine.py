"""The Database facade and per-session connections.

``Database`` wires every subsystem together and runs recovery on open:

    DiskManager  ->  BufferPool  ->  (LogManager / LockManager)
                          |
                      Catalog (tables, heaps, indexes)
                          |
                Executor + cost-based Optimizer

On startup it loads catalog metadata, replays the WAL (ARIES recovery),
reconciles each table's page list with what the log touched, and rebuilds
indexes from the recovered heaps.

A ``Connection`` is a session: it owns the current transaction. Statements run
in autocommit unless wrapped in an explicit BEGIN ... COMMIT/ROLLBACK. Multiple
connections share the lock manager, so concurrent sessions observe real strict
2PL (and deadlock detection).
"""

from __future__ import annotations

import os
from typing import Optional

from .catalog.catalog import Catalog
from .sql import ast
from .sql.context import ExecutionContext
from .sql.executor import ExecResult, Executor
from .sql.parser import Parser
from .storage.buffer_pool import BufferPool
from .storage.disk_manager import DiskManager
from .txn.lock_manager import LockManager
from .txn.transaction import TransactionManager
from .wal.log_manager import LogManager
from .wal.log_record import LogRecord, LogType
from .wal.recovery import RecoveryManager


class Database:
    def __init__(self, path_prefix: str, pool_size: int = 256, recover: bool = True):
        self.path_prefix = path_prefix
        parent = os.path.dirname(path_prefix)
        if parent:
            os.makedirs(parent, exist_ok=True)
        self.disk = DiskManager(path_prefix + ".db")
        self.buffer_pool = BufferPool(self.disk, pool_size=pool_size)
        self.log = LogManager(path_prefix + ".wal")
        self.buffer_pool.set_log_manager(self.log)
        self.lock_manager = LockManager()
        self.txn_manager = TransactionManager(self.log, self.lock_manager, self.buffer_pool)
        self.catalog = Catalog(self.buffer_pool, path_prefix + ".catalog.json")
        self.catalog.load()
        self.executor = Executor()
        self.recovery_report = None

        if recover:
            report = RecoveryManager(self.log, self.buffer_pool).recover()
            for table_name, pages in report.table_pages.items():
                self.catalog.adopt_pages(table_name, pages)
            if report.table_pages:
                self.catalog.persist()
            self.recovery_report = report
        self.catalog.rebuild_all_indexes()

    def connect(self) -> "Connection":
        return Connection(self)

    # -- durability ---------------------------------------------------------
    def checkpoint(self) -> None:
        """Flush all dirty pages and the catalog, then mark a checkpoint."""
        self.buffer_pool.flush_all()
        self.log.append(LogRecord(type=LogType.CHECKPOINT))
        self.log.flush()
        self.catalog.persist()

    def close(self) -> None:
        try:
            self.checkpoint()
        finally:
            self.log.close()
            self.disk.close()

    def __enter__(self) -> "Database":
        return self

    def __exit__(self, *exc) -> None:
        self.close()


class Connection:
    def __init__(self, db: Database):
        self.db = db
        self.txn = None  # type: Optional[object]

    @property
    def in_transaction(self) -> bool:
        return self.txn is not None

    def execute(self, sql: str) -> ExecResult:
        stripped = sql.strip().rstrip(";").strip()
        if stripped[:8].upper() == "EXPLAIN ":
            return self._explain(stripped[8:])
        stmt = Parser(stripped).parse()
        return self._run(stmt)

    def _explain(self, sql: str) -> ExecResult:
        stmt = Parser(sql).parse()
        if not isinstance(stmt, ast.Select):
            raise ValueError("EXPLAIN supports SELECT only")
        ctx = ExecutionContext(self.db.catalog, self.txn)
        return ExecResult("ddl", message=self.db.executor.explain(stmt, ctx))

    def _run(self, stmt) -> ExecResult:
        tm = self.db.txn_manager
        if isinstance(stmt, ast.Begin):
            if self.txn is None:
                self.txn = tm.begin()
            return ExecResult("ddl", message="BEGIN")
        if isinstance(stmt, ast.Commit):
            if self.txn is not None:
                tm.commit(self.txn)
                self.txn = None
            return ExecResult("ddl", message="COMMIT")
        if isinstance(stmt, ast.Rollback):
            if self.txn is not None:
                tm.abort(self.txn)
                self.txn = None
            return ExecResult("ddl", message="ROLLBACK")

        if self.txn is not None:                     # inside an explicit txn
            ctx = ExecutionContext(self.db.catalog, self.txn)
            return self.db.executor.execute(stmt, ctx)

        # Autocommit: one statement, one transaction.
        txn = tm.begin()
        ctx = ExecutionContext(self.db.catalog, txn)
        try:
            result = self.db.executor.execute(stmt, ctx)
            tm.commit(txn)
            return result
        except Exception:
            tm.abort(txn)
            raise

    # Convenience wrappers used by demos/tests.
    def begin(self) -> None:
        self._run(ast.Begin())

    def commit(self) -> None:
        self._run(ast.Commit())

    def rollback(self) -> None:
        self._run(ast.Rollback())
