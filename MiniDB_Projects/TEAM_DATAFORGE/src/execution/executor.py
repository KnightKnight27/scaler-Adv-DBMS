"""
Executor — Executes physical plans against the storage engine.

Takes a PhysicalPlan from the optimizer and builds an operator tree,
executing it to produce query results. Handles all statement types:
SELECT, INSERT, DELETE, UPDATE.
"""

from typing import Optional, List
from src.catalog.catalog import Catalog, ColumnInfo
from src.storage.heap_file import HeapFile
from src.storage.buffer_pool import BufferPool
from src.storage.disk_manager import DiskManager
from src.index.bplus_tree import BPlusTree
from src.parser.ast_nodes import *
from src.optimizer.plan_generator import PhysicalPlan, PlanNode, PlanNodeType
from .operators import (
    Row, Operator, SeqScanOperator, IndexScanOperator, FilterOperator,
    ProjectOperator, NestedLoopJoinOperator, SortOperator, LimitOperator,
    AggregateOperator, evaluate_expression
)


class ExecutionResult:
    """Result of query execution."""

    def __init__(self, rows: list = None, columns: list = None,
                 affected_rows: int = 0, message: str = ''):
        self.rows = rows or []
        self.columns = columns or []
        self.affected_rows = affected_rows
        self.message = message

    def __repr__(self):
        if self.rows:
            return f"ExecutionResult({len(self.rows)} rows, columns={self.columns})"
        return f"ExecutionResult(affected={self.affected_rows}, msg={self.message})"


class Executor:
    """
    Query executor — builds and runs operator trees from physical plans.

    Usage:
        executor = Executor(catalog, disk_manager, buffer_pool, indexes)
        result = executor.execute(plan)
    """

    def __init__(self, catalog: Catalog, disk_manager: DiskManager,
                 buffer_pool: BufferPool, indexes: dict = None,
                 transaction_manager=None, recovery_manager=None):
        """
        Args:
            catalog: System catalog.
            disk_manager: Disk manager for page I/O.
            buffer_pool: Buffer pool for page caching.
            indexes: Dict of {(table_name, column_name): BPlusTree}.
            transaction_manager: Optional transaction manager.
            recovery_manager: Optional recovery manager for WAL.
        """
        self.catalog = catalog
        self.disk_manager = disk_manager
        self.buffer_pool = buffer_pool
        self.indexes = indexes or {}
        self.txn_manager = transaction_manager
        self.recovery_manager = recovery_manager
        self._heap_files: dict = {}  # table_name -> HeapFile

    def get_heap_file(self, table_name: str) -> HeapFile:
        """Get or create a HeapFile for a table."""
        table_name = table_name.lower()
        if table_name not in self._heap_files:
            table_info = self.catalog.get_table(table_name)
            if table_info is None:
                raise ValueError(f"Table '{table_name}' does not exist")
            col_types = table_info.get_column_types()
            self._heap_files[table_name] = HeapFile(
                table_name, self.disk_manager, self.buffer_pool, col_types
            )
        return self._heap_files[table_name]

    def execute(self, plan: PhysicalPlan, txn_id: int = None) -> ExecutionResult:
        """
        Execute a physical plan.

        Args:
            plan: The physical plan to execute.
            txn_id: Optional transaction ID for transaction context.

        Returns:
            ExecutionResult with rows and/or status message.
        """
        if plan.statement_type == 'SELECT':
            return self._execute_select(plan)
        elif plan.statement_type == 'INSERT':
            return self._execute_insert(plan, txn_id)
        elif plan.statement_type == 'DELETE':
            return self._execute_delete(plan, txn_id)
        elif plan.statement_type == 'UPDATE':
            return self._execute_update(plan, txn_id)
        else:
            raise ValueError(f"Unknown statement type: {plan.statement_type}")

    # ─── SELECT execution ────────────────────────────────────────────

    def _execute_select(self, plan: PhysicalPlan) -> ExecutionResult:
        """Execute a SELECT plan."""
        operator = self._build_operator_tree(plan.root)
        operator.open()

        rows = []
        columns = None
        while True:
            row = operator.next()
            if row is None:
                break
            if columns is None:
                columns = [k for k in row.keys() if not k.startswith('__')]
            rows.append(row)

        operator.close()

        # Clean up rows: remove internal keys and duplicate qualified names
        clean_rows = []
        for row in rows:
            clean = {}
            for k, v in row.items():
                if not k.startswith('__'):
                    clean[k] = v
            clean_rows.append(clean)

        return ExecutionResult(rows=clean_rows, columns=columns or [])

    # ─── INSERT execution ────────────────────────────────────────────

    def _execute_insert(self, plan: PhysicalPlan, txn_id: int = None) -> ExecutionResult:
        """Execute an INSERT plan."""
        node = plan.root
        table_name = node.table_name.lower()
        table_info = self.catalog.get_table(table_name)

        if table_info is None:
            raise ValueError(f"Table '{table_name}' does not exist")

        heap_file = self.get_heap_file(table_name)
        col_names = table_info.get_column_names()

        count = 0
        for value_row in node.values:
            # Evaluate expressions to get actual values
            row_values = []
            for expr in value_row:
                if isinstance(expr, Literal):
                    row_values.append(expr.value)
                else:
                    row_values.append(evaluate_expression(expr, Row()))

            # If insert columns specified, map to full row
            if node.insert_columns:
                full_values = [None] * len(col_names)
                for i, col in enumerate(node.insert_columns):
                    col_idx = table_info.get_column_index(col)
                    if col_idx >= 0 and i < len(row_values):
                        full_values[col_idx] = row_values[i]
                row_values = full_values

            # WAL logging
            if self.recovery_manager and txn_id is not None:
                self.recovery_manager.log_insert(txn_id, table_name, row_values)

            # Insert into heap file
            rid = heap_file.insert_record(row_values)

            # Update indexes
            pk_col = table_info.primary_key
            if pk_col:
                pk_idx = table_info.get_column_index(pk_col)
                if pk_idx >= 0 and pk_idx < len(row_values):
                    key = (table_name, pk_col.lower())
                    if key in self.indexes:
                        self.indexes[key].insert(row_values[pk_idx], rid)

            # Update other indexes
            for idx_info in table_info.indexes:
                if not idx_info.is_primary:
                    col_idx = table_info.get_column_index(idx_info.column_name)
                    if col_idx >= 0:
                        key = (table_name, idx_info.column_name.lower())
                        if key in self.indexes:
                            self.indexes[key].insert(row_values[col_idx], rid)

            count += 1

        # Update statistics
        if table_info.stats:
            table_info.stats.row_count += count

        return ExecutionResult(affected_rows=count, message=f"Inserted {count} row(s)")

    # ─── DELETE execution ────────────────────────────────────────────

    def _execute_delete(self, plan: PhysicalPlan, txn_id: int = None) -> ExecutionResult:
        """Execute a DELETE plan."""
        node = plan.root
        table_name = node.table_name.lower()
        table_info = self.catalog.get_table(table_name)

        if table_info is None:
            raise ValueError(f"Table '{table_name}' does not exist")

        heap_file = self.get_heap_file(table_name)

        # Scan for rows to delete using the child operator
        scan_op = self._build_operator_tree(node.children[0])
        scan_op.open()

        rids_to_delete = []
        rows_to_delete = []
        while True:
            row = scan_op.next()
            if row is None:
                break
            if '__rid__' in row:
                rids_to_delete.append(row['__rid__'])
                rows_to_delete.append(row)
        scan_op.close()

        # Delete each row
        count = 0
        for i, rid in enumerate(rids_to_delete):
            # WAL logging
            if self.recovery_manager and txn_id is not None:
                self.recovery_manager.log_delete(txn_id, table_name, rid)

            heap_file.delete_record(rid)

            # Remove from indexes
            row = rows_to_delete[i]
            pk_col = table_info.primary_key
            if pk_col:
                key = (table_name, pk_col.lower())
                if key in self.indexes:
                    pk_val = row.get_value(pk_col)
                    if pk_val is not None:
                        self.indexes[key].delete(pk_val)

            count += 1

        if table_info.stats:
            table_info.stats.row_count = max(0, table_info.stats.row_count - count)

        return ExecutionResult(affected_rows=count, message=f"Deleted {count} row(s)")

    # ─── UPDATE execution ────────────────────────────────────────────

    def _execute_update(self, plan: PhysicalPlan, txn_id: int = None) -> ExecutionResult:
        """Execute an UPDATE plan."""
        node = plan.root
        table_name = node.table_name.lower()
        table_info = self.catalog.get_table(table_name)
        stmt = plan.original_ast

        if table_info is None:
            raise ValueError(f"Table '{table_name}' does not exist")

        heap_file = self.get_heap_file(table_name)
        col_names = table_info.get_column_names()

        # Scan for rows to update
        scan_op = self._build_operator_tree(node.children[0])
        scan_op.open()

        updates = []  # (rid, old_values, row)
        while True:
            row = scan_op.next()
            if row is None:
                break
            if '__rid__' in row:
                rid = row['__rid__']
                old_values = heap_file.get_record(rid)
                if old_values:
                    updates.append((rid, old_values, row))
        scan_op.close()

        count = 0
        for rid, old_values, row in updates:
            new_values = list(old_values)

            for col_name, val_expr in stmt.assignments:
                col_idx = table_info.get_column_index(col_name)
                if col_idx >= 0:
                    new_val = evaluate_expression(val_expr, row)
                    new_values[col_idx] = new_val

            # WAL logging
            if self.recovery_manager and txn_id is not None:
                self.recovery_manager.log_update(txn_id, table_name, rid, old_values, new_values)

            heap_file.update_record(rid, new_values)
            count += 1

        return ExecutionResult(affected_rows=count, message=f"Updated {count} row(s)")

    # ─── Operator Tree Builder ───────────────────────────────────────

    def _build_operator_tree(self, node: PlanNode) -> Operator:
        """
        Recursively build an operator tree from a plan node.
        """
        if node.node_type == PlanNodeType.SEQ_SCAN:
            return self._build_seq_scan(node)

        elif node.node_type == PlanNodeType.INDEX_SCAN:
            return self._build_index_scan(node)

        elif node.node_type == PlanNodeType.FILTER:
            child_op = self._build_operator_tree(node.children[0])
            return FilterOperator(child_op, node.predicate)

        elif node.node_type == PlanNodeType.PROJECT:
            if node.children:
                child_op = self._build_operator_tree(node.children[0])
                return ProjectOperator(child_op, node.select_items)
            else:
                # Project without child (e.g., SELECT 1+1)
                return self._build_constant_scan(node.select_items)

        elif node.node_type == PlanNodeType.NESTED_LOOP_JOIN:
            outer_op = self._build_operator_tree(node.children[0])
            inner_op = self._build_operator_tree(node.children[1])
            return NestedLoopJoinOperator(outer_op, inner_op,
                                          node.join_condition, node.join_type)

        elif node.node_type == PlanNodeType.SORT:
            child_op = self._build_operator_tree(node.children[0])
            return SortOperator(child_op, node.order_by)

        elif node.node_type == PlanNodeType.LIMIT:
            child_op = self._build_operator_tree(node.children[0])
            return LimitOperator(child_op, node.limit_count)

        elif node.node_type == PlanNodeType.AGGREGATE:
            child_op = self._build_operator_tree(node.children[0])
            return AggregateOperator(child_op, node.group_by,
                                     node.select_items, node.having)

        else:
            raise ValueError(f"Cannot build operator for: {node.node_type}")

    def _build_seq_scan(self, node: PlanNode) -> SeqScanOperator:
        table_name = node.table_name.lower()
        table_info = self.catalog.get_table(table_name)
        if table_info is None:
            raise ValueError(f"Table '{table_name}' not found")
        heap_file = self.get_heap_file(table_name)
        col_names = table_info.get_column_names()
        return SeqScanOperator(heap_file, table_name, col_names,
                               table_alias=node.table_alias)

    def _build_index_scan(self, node: PlanNode) -> Operator:
        table_name = node.table_name.lower()
        table_info = self.catalog.get_table(table_name)
        if table_info is None:
            raise ValueError(f"Table '{table_name}' not found")

        heap_file = self.get_heap_file(table_name)
        col_names = table_info.get_column_names()
        col_types = table_info.get_column_types()

        index_key = (table_name, node.index_column.lower())
        btree = self.indexes.get(index_key)

        if btree is None:
            # Fall back to seq scan if index not available
            scan = SeqScanOperator(heap_file, table_name, col_names,
                                    table_alias=node.table_alias)
            if node.predicate:
                return FilterOperator(scan, node.predicate)
            return scan

        op = IndexScanOperator(
            btree, heap_file, table_name, col_names, col_types,
            node.index_column, lookup_key=node.index_key,
            table_alias=node.table_alias,
        )

        # May still need additional filtering if the WHERE has multiple conditions
        if node.predicate:
            return FilterOperator(op, node.predicate)

        return op

    def _build_constant_scan(self, select_items) -> Operator:
        """Build a scan that returns a single row with constant expressions."""
        class ConstantScan(Operator):
            def __init__(self, items):
                self.items = items
                self._done = False
            def open(self):
                self._done = False
            def next(self):
                if self._done:
                    return None
                self._done = True
                row = Row()
                for item in self.items:
                    val = evaluate_expression(item.expr, Row())
                    key = item.alias or str(item.expr)
                    row[key] = val
                return row
            def close(self):
                pass
        return ConstantScan(select_items)
