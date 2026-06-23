"""
Query planner: bridges parser AST → executor.
Handles CREATE TABLE, DROP TABLE, BEGIN/COMMIT/ROLLBACK at this layer.
SELECT/INSERT/DELETE are passed to the Executor.
"""
from sql.parser import (
    parse, SelectStmt, InsertStmt, DeleteStmt,
    CreateTableStmt, DropTableStmt, BeginStmt, CommitStmt, RollbackStmt,
)
from catalog.catalog import Catalog, Column


class Planner:
    def __init__(self, db):
        self.db = db  # reference to MiniDB for DDL side-effects

    def plan_and_execute(self, sql: str, txn=None):
        stmt = parse(sql)

        if isinstance(stmt, BeginStmt):
            return self.db.begin(), 'BEGIN'

        if isinstance(stmt, CommitStmt):
            if txn:
                self.db.commit(txn)
            return None, 'COMMIT'

        if isinstance(stmt, RollbackStmt):
            if txn:
                self.db.rollback(txn)
            return None, 'ROLLBACK'

        if isinstance(stmt, CreateTableStmt):
            return self.db.create_table(stmt), 'CREATE TABLE'

        if isinstance(stmt, DropTableStmt):
            return self.db.drop_table(stmt.table), 'DROP TABLE'

        # SELECT is read-only: skip WAL logging entirely (no fsync overhead)
        read_only = isinstance(stmt, SelectStmt)

        if txn is None:
            txn = self.db.begin(read_only=read_only)
            auto_commit = True
        else:
            auto_commit = False

        try:
            result = self.db.executor.execute(stmt, txn)
            if auto_commit:
                # read-only: just release txn, no WAL write
                if read_only:
                    self.db.txn_manager.abort(txn)
                else:
                    self.db.commit(txn)
            return result, type(stmt).__name__
        except Exception:
            if auto_commit:
                self.db.rollback(txn)
            raise
