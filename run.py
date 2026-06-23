#!/usr/bin/env python3
"""
MiniDB — Interactive SQL Shell (REPL)

A complete relational database engine built from scratch.

Usage:
    python run.py [--db-dir <path>] [--pool-size <N>]

Commands:
    SQL statements: SELECT, INSERT, DELETE, UPDATE, CREATE TABLE, DROP TABLE
    .tables                 List all tables
    .schema <table>         Show table schema
    .stats [table]          Show statistics
    .indexes                Show all indexes
    .explain <SQL>          Show query plan
    .buffer                 Buffer pool stats
    .wal                    WAL log summary
    .demo                   Run demo with sample data
    .mvcc                   Show MVCC statistics
    .help                   Show help
    .quit / .exit           Exit
"""

import sys
import os
import time
import argparse

# Add project root to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from src.storage.disk_manager import DiskManager
from src.storage.buffer_pool import BufferPool
from src.storage.heap_file import HeapFile
from src.catalog.catalog import Catalog, ColumnInfo
from src.index.bplus_tree import BPlusTree
from src.parser.parser import Parser, ParseError
from src.parser.lexer import LexerError
from src.parser.ast_nodes import *
from src.optimizer.cost_estimator import CostEstimator
from src.optimizer.plan_generator import PlanGenerator
from src.optimizer.statistics import TableStatistics
from src.execution.executor import Executor
from src.transaction.lock_manager import LockManager
from src.transaction.transaction_manager import TransactionManager
from src.transaction.mvcc import MVCCManager
from src.recovery.wal import WAL
from src.recovery.recovery_manager import RecoveryManager


class MiniDB:
    """
    MiniDB Engine — orchestrates all database components.
    """

    def __init__(self, db_dir: str = './minidb_data', pool_size: int = 100):
        self.db_dir = db_dir
        os.makedirs(db_dir, exist_ok=True)

        # Core components
        self.disk_manager = DiskManager(db_dir)
        self.buffer_pool = BufferPool(self.disk_manager, pool_size)
        self.catalog = Catalog(db_dir)

        # Indexes: {(table_name, column_name): BPlusTree}
        self.indexes: dict = {}

        # WAL and Recovery
        self.wal = WAL(db_dir)
        self.recovery_manager = RecoveryManager(self.wal)

        # Transaction Management
        self.lock_manager = LockManager()
        self.txn_manager = TransactionManager(self.lock_manager, self.recovery_manager)

        # MVCC (Extension Track B)
        self.mvcc = MVCCManager()

        # Optimizer
        self.cost_estimator = CostEstimator(self.catalog)
        self.plan_generator = PlanGenerator(self.catalog, self.cost_estimator)
        self.statistics = TableStatistics(self.catalog)

        # Executor
        self.executor = Executor(
            self.catalog, self.disk_manager, self.buffer_pool,
            self.indexes, self.txn_manager, self.recovery_manager
        )

        # Current transaction context
        self._current_txn_id = None

        # Rebuild indexes from catalog
        self._rebuild_indexes()

        # Perform crash recovery
        self._recover()

    def _rebuild_indexes(self):
        """Rebuild in-memory B+ Tree indexes from existing data."""
        for table_name in self.catalog.list_tables():
            table_info = self.catalog.get_table(table_name)
            if table_info is None:
                continue

            heap_file = self.executor.get_heap_file(table_name)

            for idx_info in table_info.indexes:
                col_idx = table_info.get_column_index(idx_info.column_name)
                if col_idx < 0:
                    continue

                btree = BPlusTree(order=50)
                for rid, values in heap_file.scan():
                    if col_idx < len(values) and values[col_idx] is not None:
                        btree.insert(values[col_idx], rid)

                key = (table_name, idx_info.column_name.lower())
                self.indexes[key] = btree

        # Update executor's index reference
        self.executor.indexes = self.indexes

    def _recover(self):
        """Perform crash recovery using the WAL."""
        try:
            committed, active = self.recovery_manager.recover()
            if committed or active:
                print(f"[Recovery] Recovered: {len(committed)} committed, "
                      f"{len(active)} aborted transactions")
        except Exception as e:
            print(f"[Recovery] Warning: {e}")

    def execute_sql(self, sql: str) -> str:
        """
        Execute a SQL statement and return formatted results.

        Args:
            sql: The SQL string.

        Returns:
            Formatted result string.
        """
        sql = sql.strip()
        if not sql:
            return ''

        try:
            parser = Parser(sql)
            stmt = parser.parse()
        except (ParseError, LexerError) as e:
            return f"ERROR: {e}"

        try:
            # Handle DDL statements
            if isinstance(stmt, CreateTableStatement):
                return self._handle_create_table(stmt)
            elif isinstance(stmt, DropTableStatement):
                return self._handle_drop_table(stmt)
            elif isinstance(stmt, CreateIndexStatement):
                return self._handle_create_index(stmt)
            elif isinstance(stmt, BeginStatement):
                return self._handle_begin()
            elif isinstance(stmt, CommitStatement):
                return self._handle_commit()
            elif isinstance(stmt, RollbackStatement):
                return self._handle_rollback()

            # DML: generate plan and execute
            plan = self.plan_generator.generate(stmt)
            result = self.executor.execute(plan, self._current_txn_id)

            if result.rows:
                return self._format_table(result.rows, result.columns)
            else:
                return result.message

        except Exception as e:
            return f"ERROR: {e}"

    def _handle_create_table(self, stmt: CreateTableStatement) -> str:
        """Handle CREATE TABLE statement."""
        columns = []
        pk = None
        for col_def in stmt.columns:
            col = ColumnInfo(
                name=col_def['name'].lower(),
                data_type=col_def['type'],
                is_primary_key=col_def.get('primary_key', False),
                is_nullable=col_def.get('nullable', True),
            )
            columns.append(col)
            if col.is_primary_key:
                pk = col.name

        table_info = self.catalog.create_table(stmt.table_name.lower(), columns, pk)

        # Create primary key B+ Tree index
        if pk:
            btree = BPlusTree(order=50)
            key = (stmt.table_name.lower(), pk.lower())
            self.indexes[key] = btree
            self.executor.indexes = self.indexes

        # Create the heap file
        self.executor.get_heap_file(stmt.table_name.lower())

        return f"Table '{stmt.table_name}' created successfully"

    def _handle_drop_table(self, stmt: DropTableStatement) -> str:
        """Handle DROP TABLE statement."""
        table_name = stmt.table_name.lower()
        table_info = self.catalog.get_table(table_name)
        if table_info is None:
            return f"ERROR: Table '{stmt.table_name}' does not exist"

        # Remove indexes
        for idx_info in table_info.indexes:
            key = (table_name, idx_info.column_name.lower())
            self.indexes.pop(key, None)

        # Remove heap file
        self.disk_manager.delete_file(f"{table_name}.db")

        # Remove from catalog
        self.catalog.drop_table(table_name)

        # Clear from executor cache
        if table_name in self.executor._heap_files:
            del self.executor._heap_files[table_name]

        self.executor.indexes = self.indexes
        return f"Table '{stmt.table_name}' dropped"

    def _handle_create_index(self, stmt: CreateIndexStatement) -> str:
        """Handle CREATE INDEX statement."""
        table_name = stmt.table_name.lower()
        col_name = stmt.column_name.lower()
        table_info = self.catalog.get_table(table_name)

        if table_info is None:
            return f"ERROR: Table '{stmt.table_name}' does not exist"

        col = table_info.get_column(col_name)
        if col is None:
            return f"ERROR: Column '{stmt.column_name}' does not exist in '{stmt.table_name}'"

        # Register in catalog
        self.catalog.create_index(stmt.index_name.lower(), table_name, col_name)

        # Build B+ Tree from existing data
        btree = BPlusTree(order=50)
        heap_file = self.executor.get_heap_file(table_name)
        col_idx = table_info.get_column_index(col_name)

        for rid, values in heap_file.scan():
            if col_idx < len(values) and values[col_idx] is not None:
                btree.insert(values[col_idx], rid)

        key = (table_name, col_name)
        self.indexes[key] = btree
        self.executor.indexes = self.indexes

        return f"Index '{stmt.index_name}' created on {stmt.table_name}({stmt.column_name})"

    def _handle_begin(self) -> str:
        """Handle BEGIN TRANSACTION."""
        if self._current_txn_id is not None:
            return "ERROR: Transaction already in progress"
        self._current_txn_id = self.txn_manager.begin()
        self.mvcc.begin_transaction(self._current_txn_id,
                                     self.txn_manager.get_snapshot_ts(self._current_txn_id))
        return f"Transaction {self._current_txn_id} started"

    def _handle_commit(self) -> str:
        """Handle COMMIT."""
        if self._current_txn_id is None:
            return "ERROR: No active transaction"
        txn_id = self._current_txn_id
        self.txn_manager.commit(txn_id)
        self.mvcc.commit_transaction(txn_id)
        self._current_txn_id = None
        return f"Transaction {txn_id} committed"

    def _handle_rollback(self) -> str:
        """Handle ROLLBACK."""
        if self._current_txn_id is None:
            return "ERROR: No active transaction"
        txn_id = self._current_txn_id
        self.txn_manager.abort(txn_id)
        self.mvcc.abort_transaction(txn_id)
        self._current_txn_id = None
        return f"Transaction {txn_id} rolled back"

    def explain(self, sql: str) -> str:
        """Show the query execution plan for a SQL statement."""
        try:
            parser = Parser(sql)
            stmt = parser.parse()
            if isinstance(stmt, (SelectStatement, InsertStatement,
                                DeleteStatement, UpdateStatement)):
                plan = self.plan_generator.generate(stmt)
                return PlanGenerator.explain(plan)
            return "EXPLAIN only available for SELECT/INSERT/DELETE/UPDATE"
        except Exception as e:
            return f"ERROR: {e}"

    def update_statistics(self, table_name: str = None):
        """Update statistics for one or all tables."""
        tables = [table_name] if table_name else self.catalog.list_tables()
        for tname in tables:
            try:
                heap_file = self.executor.get_heap_file(tname)
                self.statistics.analyze_table(tname, heap_file)
            except Exception:
                pass

    def _format_table(self, rows: list, columns: list) -> str:
        """Format query results as a table."""
        if not rows:
            return "(0 rows)"

        # Deduplicate columns: prefer unqualified names
        clean_columns = []
        seen = set()
        for col in columns:
            base = col.split('.')[-1] if '.' in col else col
            if base not in seen:
                clean_columns.append(base)
                seen.add(base)

        # Build table data
        table_data = []
        for row in rows:
            row_data = []
            for col in clean_columns:
                val = None
                # Try exact match
                if col in row:
                    val = row[col]
                else:
                    # Try qualified names
                    for k, v in row.items():
                        if k.endswith(f".{col}") or k == col:
                            val = v
                            break
                row_data.append(str(val) if val is not None else 'NULL')
            table_data.append(row_data)

        # Calculate column widths
        widths = [len(c) for c in clean_columns]
        for row_data in table_data:
            for i, val in enumerate(row_data):
                if i < len(widths):
                    widths[i] = max(widths[i], len(val))

        # Format output
        lines = []
        # Header
        header = ' | '.join(col.ljust(widths[i]) for i, col in enumerate(clean_columns))
        lines.append(header)
        lines.append('-+-'.join('-' * w for w in widths))
        # Data
        for row_data in table_data:
            line = ' | '.join(
                (row_data[i] if i < len(row_data) else '').ljust(widths[i])
                for i in range(len(clean_columns))
            )
            lines.append(line)
        lines.append(f"({len(rows)} row{'s' if len(rows) != 1 else ''})")

        return '\n'.join(lines)

    def run_demo(self) -> str:
        """Load demo data for testing."""
        results = []

        # Create tables
        results.append(self.execute_sql("""
            CREATE TABLE departments (
                dept_id INTEGER PRIMARY KEY,
                dept_name VARCHAR NOT NULL,
                budget FLOAT
            )
        """))

        results.append(self.execute_sql("""
            CREATE TABLE employees (
                id INTEGER PRIMARY KEY,
                name VARCHAR NOT NULL,
                dept_id INTEGER,
                salary FLOAT,
                active BOOLEAN
            )
        """))

        results.append(self.execute_sql("""
            CREATE TABLE projects (
                project_id INTEGER PRIMARY KEY,
                project_name VARCHAR NOT NULL,
                dept_id INTEGER,
                budget FLOAT
            )
        """))

        # Insert departments
        results.append(self.execute_sql(
            "INSERT INTO departments (dept_id, dept_name, budget) VALUES "
            "(1, 'Engineering', 500000.0), "
            "(2, 'Marketing', 300000.0), "
            "(3, 'Sales', 400000.0), "
            "(4, 'HR', 200000.0)"
        ))

        # Insert employees
        employees = [
            (1, 'Alice Johnson', 1, 95000.0, True),
            (2, 'Bob Smith', 1, 88000.0, True),
            (3, 'Charlie Brown', 2, 72000.0, True),
            (4, 'Diana Ross', 2, 78000.0, True),
            (5, 'Eve Davis', 3, 65000.0, True),
            (6, 'Frank Miller', 3, 70000.0, False),
            (7, 'Grace Lee', 1, 110000.0, True),
            (8, 'Henry Wilson', 4, 60000.0, True),
            (9, 'Irene Clark', 1, 92000.0, True),
            (10, 'Jack Turner', 2, 85000.0, True),
        ]
        for emp in employees:
            results.append(self.execute_sql(
                f"INSERT INTO employees (id, name, dept_id, salary, active) VALUES "
                f"({emp[0]}, '{emp[1]}', {emp[2]}, {emp[3]}, {str(emp[4]).upper()})"
            ))

        # Insert projects
        results.append(self.execute_sql(
            "INSERT INTO projects (project_id, project_name, dept_id, budget) VALUES "
            "(101, 'Project Alpha', 1, 150000.0), "
            "(102, 'Project Beta', 2, 80000.0), "
            "(103, 'Project Gamma', 1, 200000.0), "
            "(104, 'Project Delta', 3, 120000.0)"
        ))

        # Update statistics
        self.update_statistics()

        return "Demo data loaded: 4 departments, 10 employees, 4 projects"


def main():
    """Main entry point — run the interactive MiniDB shell."""
    arg_parser = argparse.ArgumentParser(description='MiniDB — A relational database engine')
    arg_parser.add_argument('--db-dir', default='./minidb_data',
                           help='Database directory (default: ./minidb_data)')
    arg_parser.add_argument('--pool-size', type=int, default=100,
                           help='Buffer pool size (default: 100)')
    args = arg_parser.parse_args()

    print("=" * 65)
    print("  MiniDB — Relational Database Engine")
    print("  Team DataForge | Advanced DBMS Capstone Project")
    print("  Extension Track B: MVCC (Multi-Version Concurrency Control)")
    print("=" * 65)
    print()
    print("  Type SQL statements or use dot-commands (.help for list)")
    print("  Type .demo to load sample data")
    print()

    db = MiniDB(db_dir=args.db_dir, pool_size=args.pool_size)

    while True:
        try:
            # Show transaction context if active
            prompt = f"minidb [txn:{db._current_txn_id}]> " if db._current_txn_id else "minidb> "
            sql = input(prompt).strip()

            if not sql:
                continue

            # Dot commands
            if sql.startswith('.'):
                cmd_parts = sql.split(maxsplit=1)
                cmd = cmd_parts[0].lower()
                cmd_arg = cmd_parts[1] if len(cmd_parts) > 1 else None

                if cmd in ('.quit', '.exit'):
                    print("Goodbye!")
                    db.buffer_pool.flush_all()
                    db.wal.close()
                    db.disk_manager.close()
                    break

                elif cmd == '.help':
                    print(__doc__)

                elif cmd == '.tables':
                    tables = db.catalog.list_tables()
                    if tables:
                        for t in tables:
                            info = db.catalog.get_table(t)
                            rows = info.stats.row_count if info.stats else '?'
                            print(f"  {t} ({rows} rows)")
                    else:
                        print("  (no tables)")

                elif cmd == '.schema':
                    if cmd_arg:
                        info = db.catalog.get_table(cmd_arg.lower())
                        if info:
                            print(f"\n  Table: {info.name}")
                            print(f"  Primary Key: {info.primary_key or 'none'}")
                            print(f"  Columns:")
                            for col in info.columns:
                                pk = ' PRIMARY KEY' if col.is_primary_key else ''
                                nullable = '' if col.is_nullable else ' NOT NULL'
                                print(f"    {col.name}: {col.data_type}{pk}{nullable}")
                            print(f"  Indexes:")
                            for idx in info.indexes:
                                print(f"    {idx.name} on ({idx.column_name})"
                                      f"{' [PRIMARY]' if idx.is_primary else ''}")
                        else:
                            print(f"  Table '{cmd_arg}' not found")
                    else:
                        print("  Usage: .schema <table_name>")

                elif cmd == '.stats':
                    table_name = cmd_arg.lower() if cmd_arg else None
                    if table_name:
                        db.update_statistics(table_name)
                        info = db.catalog.get_table(table_name)
                        if info and info.stats:
                            s = info.stats
                            print(f"\n  Statistics for '{table_name}':")
                            print(f"    Rows: {s.row_count}")
                            print(f"    Pages: {s.page_count}")
                            print(f"    Distinct values: {s.distinct_values}")
                            print(f"    Min values: {s.min_values}")
                            print(f"    Max values: {s.max_values}")
                    else:
                        for t in db.catalog.list_tables():
                            db.update_statistics(t)
                            info = db.catalog.get_table(t)
                            if info and info.stats:
                                print(f"  {t}: {info.stats.row_count} rows, "
                                      f"{info.stats.page_count} pages")

                elif cmd == '.indexes':
                    for (table, col), btree in db.indexes.items():
                        print(f"  {table}.{col}: {len(btree)} entries, "
                              f"height={btree.height}")

                elif cmd == '.explain':
                    if cmd_arg:
                        print(db.explain(cmd_arg))
                    else:
                        print("  Usage: .explain <SQL>")

                elif cmd == '.buffer':
                    stats = db.buffer_pool.get_stats()
                    print(f"\n  Buffer Pool Statistics:")
                    for k, v in stats.items():
                        print(f"    {k}: {v}")

                elif cmd == '.wal':
                    summary = db.recovery_manager.get_log_summary()
                    print(f"\n  WAL Summary:")
                    for k, v in summary.items():
                        print(f"    {k}: {v}")

                elif cmd == '.mvcc':
                    stats = db.mvcc.get_stats()
                    print(f"\n  MVCC Statistics:")
                    for k, v in stats.items():
                        print(f"    {k}: {v}")

                elif cmd == '.demo':
                    start = time.time()
                    result = db.run_demo()
                    elapsed = time.time() - start
                    print(f"  {result} ({elapsed:.3f}s)")

                elif cmd == '.txn':
                    stats = db.txn_manager.get_stats()
                    print(f"\n  Transaction Statistics:")
                    for k, v in stats.items():
                        print(f"    {k}: {v}")

                else:
                    print(f"  Unknown command: {cmd} (type .help for commands)")

            else:
                # SQL statement
                start = time.time()
                result = db.execute_sql(sql)
                elapsed = time.time() - start
                print(result)
                print(f"Time: {elapsed:.3f}s")

        except KeyboardInterrupt:
            print("\n  Use .quit to exit")
        except EOFError:
            print("\nGoodbye!")
            break
        except Exception as e:
            print(f"ERROR: {e}")


if __name__ == '__main__':
    main()
