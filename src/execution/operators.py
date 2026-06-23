"""
Physical Operators — Volcano/Iterator model operators for MiniDB.

Each operator implements the iterator interface:
  - open()  — initialize the operator
  - next()  — return the next row (or None if exhausted)
  - close() — clean up resources

Rows are represented as dictionaries mapping column names to values,
which makes join and projection operations more natural.
"""

from typing import Optional, List
from src.parser.ast_nodes import *


class Row(dict):
    """
    A row of query results, stored as {column_name: value}.
    Supports qualified names (table.column) and unqualified access.
    """

    def get_value(self, column: str, table: str = None):
        """Get a column value, trying qualified then unqualified name."""
        if table:
            qualified = f"{table}.{column}"
            if qualified in self:
                return self[qualified]
        if column in self:
            return self[column]
        # Try case-insensitive
        col_lower = column.lower()
        for k, v in self.items():
            if k.lower() == col_lower or k.lower().endswith(f".{col_lower}"):
                return v
        return None

    def has_column(self, column: str, table: str = None) -> bool:
        if table:
            qualified = f"{table}.{column}"
            if qualified in self:
                return True
        if column in self:
            return True
        col_lower = column.lower()
        for k in self.keys():
            if k.lower() == col_lower or k.lower().endswith(f".{col_lower}"):
                return True
        return False


class Operator:
    """Base class for all physical operators."""

    def open(self):
        raise NotImplementedError

    def next(self) -> Optional[Row]:
        raise NotImplementedError

    def close(self):
        raise NotImplementedError


class SeqScanOperator(Operator):
    """
    Sequential scan — reads all rows from a heap file.

    Produces rows as {table.column: value} dictionaries.
    """

    def __init__(self, heap_file, table_name: str, column_names: list, table_alias: str = None):
        self.heap_file = heap_file
        self.table_name = table_name
        self.column_names = column_names
        self.table_alias = table_alias or table_name
        self._iterator = None

    def open(self):
        self._iterator = self.heap_file.scan()

    def next(self) -> Optional[Row]:
        try:
            rid, values = next(self._iterator)
            row = Row()
            for i, col in enumerate(self.column_names):
                val = values[i] if i < len(values) else None
                row[f"{self.table_alias}.{col}"] = val
                row[col] = val  # Also store unqualified for convenience
            row['__rid__'] = rid  # Internal: for DELETE/UPDATE
            return row
        except StopIteration:
            return None

    def close(self):
        self._iterator = None


class IndexScanOperator(Operator):
    """
    Index scan — uses B+ tree index for point or range lookups.
    """

    def __init__(self, btree, heap_file, table_name: str,
                 column_names: list, column_types: list,
                 index_column: str, lookup_key=None,
                 range_low=None, range_high=None,
                 table_alias: str = None):
        self.btree = btree
        self.heap_file = heap_file
        self.table_name = table_name
        self.column_names = column_names
        self.column_types = column_types
        self.index_column = index_column
        self.lookup_key = lookup_key
        self.range_low = range_low
        self.range_high = range_high
        self.table_alias = table_alias or table_name
        self._results = []
        self._pos = 0

    def open(self):
        from src.storage.page import deserialize_record

        if self.lookup_key is not None:
            # Point lookup
            rids = self.btree.search_all(self.lookup_key)
            if not rids:
                rid = self.btree.search(self.lookup_key)
                rids = [rid] if rid else []
        elif self.range_low is not None and self.range_high is not None:
            entries = self.btree.range_search(self.range_low, self.range_high)
            rids = [rid for _, rid in entries]
        else:
            rids = []

        self._results = []
        for rid in rids:
            values = self.heap_file.get_record(rid)
            if values is not None:
                row = Row()
                for i, col in enumerate(self.column_names):
                    val = values[i] if i < len(values) else None
                    row[f"{self.table_alias}.{col}"] = val
                    row[col] = val
                row['__rid__'] = rid
                self._results.append(row)
        self._pos = 0

    def next(self) -> Optional[Row]:
        if self._pos < len(self._results):
            row = self._results[self._pos]
            self._pos += 1
            return row
        return None

    def close(self):
        self._results = []
        self._pos = 0


class FilterOperator(Operator):
    """
    Filter — applies a predicate to rows from a child operator.
    """

    def __init__(self, child: Operator, predicate: Expression):
        self.child = child
        self.predicate = predicate

    def open(self):
        self.child.open()

    def next(self) -> Optional[Row]:
        while True:
            row = self.child.next()
            if row is None:
                return None
            if evaluate_predicate(self.predicate, row):
                return row

    def close(self):
        self.child.close()


class ProjectOperator(Operator):
    """
    Project — selects specific columns / evaluates expressions.
    """

    def __init__(self, child: Operator, select_items: list):
        self.child = child
        self.select_items = select_items

    def open(self):
        self.child.open()

    def next(self) -> Optional[Row]:
        row = self.child.next()
        if row is None:
            return None

        result = Row()
        for item in self.select_items:
            if isinstance(item.expr, StarExpr):
                # SELECT * — pass through all non-internal columns
                for k, v in row.items():
                    if not k.startswith('__'):
                        result[k] = v
                return result
            else:
                val = evaluate_expression(item.expr, row)
                if item.alias:
                    result[item.alias] = val
                elif isinstance(item.expr, ColumnRef):
                    result[item.expr.column] = val
                elif isinstance(item.expr, FunctionCall):
                    result[f"{item.expr.name}"] = val
                else:
                    result[str(item.expr)] = val

        # Preserve RID for downstream operations
        if '__rid__' in row:
            result['__rid__'] = row['__rid__']

        return result

    def close(self):
        self.child.close()


class NestedLoopJoinOperator(Operator):
    """
    Nested Loop Join — for each outer row, scans all inner rows.

    Supports INNER, LEFT, RIGHT, and CROSS joins.
    """

    def __init__(self, outer: Operator, inner: Operator,
                 condition: Expression = None, join_type: str = 'INNER'):
        self.outer = outer
        self.inner = inner
        self.condition = condition
        self.join_type = join_type.upper()
        self._outer_row = None
        self._inner_rows = []
        self._inner_pos = 0
        self._outer_matched = False
        self._left_done = False

    def open(self):
        self.outer.open()
        # Materialize inner for nested loop
        self.inner.open()
        self._inner_rows = []
        while True:
            row = self.inner.next()
            if row is None:
                break
            self._inner_rows.append(row)
        self.inner.close()

        self._outer_row = None
        self._inner_pos = 0
        self._outer_matched = False
        self._left_done = False

    def next(self) -> Optional[Row]:
        while True:
            if self._outer_row is None:
                self._outer_row = self.outer.next()
                if self._outer_row is None:
                    return None
                self._inner_pos = 0
                self._outer_matched = False

            while self._inner_pos < len(self._inner_rows):
                inner_row = self._inner_rows[self._inner_pos]
                self._inner_pos += 1

                # Merge rows
                merged = Row()
                merged.update(self._outer_row)
                merged.update(inner_row)

                if self.condition is None or evaluate_predicate(self.condition, merged):
                    self._outer_matched = True
                    return merged

            # Exhausted inner for this outer row
            if self.join_type == 'LEFT' and not self._outer_matched:
                # Return outer row with NULLs for inner
                row = Row()
                row.update(self._outer_row)
                self._outer_row = None
                return row

            self._outer_row = None

    def close(self):
        self.outer.close()
        self._inner_rows = []


class SortOperator(Operator):
    """
    Sort — materializes child results and sorts them.
    """

    def __init__(self, child: Operator, order_by: list):
        self.child = child
        self.order_by = order_by
        self._sorted_rows = []
        self._pos = 0

    def open(self):
        self.child.open()
        rows = []
        while True:
            row = self.child.next()
            if row is None:
                break
            rows.append(row)
        self.child.close()

        # Sort by ORDER BY expressions
        def sort_key(row):
            key_parts = []
            for ob in self.order_by:
                val = evaluate_expression(ob.expr, row)
                if val is None:
                    val = ''  # NULLs sort first
                if not ob.ascending:
                    # For descending, negate numbers or reverse strings
                    if isinstance(val, (int, float)):
                        val = -val
                key_parts.append(val)
            return key_parts

        try:
            self._sorted_rows = sorted(rows, key=sort_key)
        except TypeError:
            self._sorted_rows = rows  # If incomparable types, keep original order
        self._pos = 0

    def next(self) -> Optional[Row]:
        if self._pos < len(self._sorted_rows):
            row = self._sorted_rows[self._pos]
            self._pos += 1
            return row
        return None

    def close(self):
        self._sorted_rows = []
        self._pos = 0


class LimitOperator(Operator):
    """
    Limit — returns at most N rows.
    """

    def __init__(self, child: Operator, limit: int):
        self.child = child
        self.limit = limit
        self._count = 0

    def open(self):
        self.child.open()
        self._count = 0

    def next(self) -> Optional[Row]:
        if self._count >= self.limit:
            return None
        row = self.child.next()
        if row is not None:
            self._count += 1
        return row

    def close(self):
        self.child.close()


class AggregateOperator(Operator):
    """
    Aggregate — GROUP BY and aggregate functions (COUNT, SUM, AVG, MIN, MAX).
    """

    def __init__(self, child: Operator, group_by: list,
                 select_items: list, having: Expression = None):
        self.child = child
        self.group_by_exprs = group_by
        self.select_items = select_items
        self.having = having
        self._results = []
        self._pos = 0

    def open(self):
        self.child.open()

        # Materialize all rows
        rows = []
        while True:
            row = self.child.next()
            if row is None:
                break
            rows.append(row)
        self.child.close()

        # Group rows
        groups = {}
        for row in rows:
            if self.group_by_exprs:
                key = tuple(evaluate_expression(expr, row) for expr in self.group_by_exprs)
            else:
                key = ('__all__',)

            if key not in groups:
                groups[key] = []
            groups[key].append(row)

        # Compute aggregates for each group
        self._results = []
        for key, group_rows in groups.items():
            result = Row()

            # Set group-by column values
            if self.group_by_exprs:
                for i, expr in enumerate(self.group_by_exprs):
                    if isinstance(expr, ColumnRef):
                        result[expr.column] = key[i]

            # Compute each select item
            for item in self.select_items:
                if isinstance(item.expr, FunctionCall):
                    val = self._compute_aggregate(item.expr, group_rows)
                    name = item.alias or item.expr.name
                    result[name] = val
                elif isinstance(item.expr, ColumnRef):
                    col_name = item.alias or item.expr.column
                    if col_name not in result and group_rows:
                        result[col_name] = evaluate_expression(item.expr, group_rows[0])
                elif isinstance(item.expr, StarExpr):
                    if group_rows:
                        for k, v in group_rows[0].items():
                            if not k.startswith('__'):
                                result[k] = v

            # Apply HAVING
            if self.having:
                if not evaluate_predicate(self.having, result):
                    continue

            self._results.append(result)

        self._pos = 0

    def _compute_aggregate(self, func: FunctionCall, rows: list):
        """Compute an aggregate function over a group of rows."""
        name = func.name.upper()

        if name == 'COUNT':
            if func.args and isinstance(func.args[0], StarExpr):
                return len(rows)
            else:
                # COUNT(column) — count non-null values
                count = 0
                for row in rows:
                    val = evaluate_expression(func.args[0], row) if func.args else None
                    if val is not None:
                        count += 1
                return count

        # Extract values for the argument
        values = []
        for row in rows:
            if func.args:
                val = evaluate_expression(func.args[0], row)
                if val is not None:
                    values.append(val)

        if not values:
            return None

        if name == 'SUM':
            return sum(values)
        elif name == 'AVG':
            return sum(values) / len(values)
        elif name == 'MIN':
            return min(values)
        elif name == 'MAX':
            return max(values)

        return None

    def next(self) -> Optional[Row]:
        if self._pos < len(self._results):
            row = self._results[self._pos]
            self._pos += 1
            return row
        return None

    def close(self):
        self._results = []
        self._pos = 0


# ─── Expression Evaluation ────────────────────────────────────────────────────

def evaluate_expression(expr: Expression, row: Row):
    """
    Evaluate an expression against a row.

    Args:
        expr: The expression to evaluate.
        row: The current row context.

    Returns:
        The computed value.
    """
    if isinstance(expr, Literal):
        return expr.value

    if isinstance(expr, ColumnRef):
        return row.get_value(expr.column, expr.table)

    if isinstance(expr, BinaryOp):
        left = evaluate_expression(expr.left, row)
        right = evaluate_expression(expr.right, row)

        op = expr.op
        if op in ('+',):
            return (left or 0) + (right or 0)
        if op in ('-',):
            return (left or 0) - (right or 0)
        if op in ('*',):
            return (left or 0) * (right or 0)
        if op in ('/',):
            if right == 0:
                return None
            return (left or 0) / (right or 0)
        # Comparisons return boolean (handled by evaluate_predicate)
        return evaluate_comparison(op, left, right)

    if isinstance(expr, UnaryOp):
        val = evaluate_expression(expr.operand, row)
        if expr.op == '-':
            return -val if val is not None else None
        if expr.op == 'NOT':
            return not val if val is not None else None
        return val

    if isinstance(expr, FunctionCall):
        # For non-aggregate use (e.g., in projection of aggregate results)
        # The aggregate should have been pre-computed
        name = expr.name.upper()
        if name in row:
            return row[name]
        return None

    if isinstance(expr, StarExpr):
        return None

    return None


def evaluate_comparison(op: str, left, right) -> bool:
    """Evaluate a comparison operation."""
    if left is None or right is None:
        return False  # NULL comparisons are false

    try:
        if op in ('=', '=='):
            return left == right
        elif op in ('!=', '<>'):
            return left != right
        elif op == '<':
            return left < right
        elif op == '>':
            return left > right
        elif op == '<=':
            return left <= right
        elif op == '>=':
            return left >= right
        elif op == 'LIKE':
            # Simple LIKE: % = any chars, _ = single char
            import re
            pattern = str(right).replace('%', '.*').replace('_', '.')
            return bool(re.fullmatch(pattern, str(left), re.IGNORECASE))
    except TypeError:
        return False

    return False


def evaluate_predicate(expr: Expression, row: Row) -> bool:
    """
    Evaluate a boolean predicate expression against a row.

    Args:
        expr: The predicate expression.
        row: The row to evaluate against.

    Returns:
        True if the row satisfies the predicate.
    """
    if expr is None:
        return True

    if isinstance(expr, BinaryOp):
        op = expr.op.upper()

        if op == 'AND':
            return (evaluate_predicate(expr.left, row) and
                    evaluate_predicate(expr.right, row))
        if op == 'OR':
            return (evaluate_predicate(expr.left, row) or
                    evaluate_predicate(expr.right, row))

        left = evaluate_expression(expr.left, row)
        right = evaluate_expression(expr.right, row)
        return evaluate_comparison(expr.op, left, right)

    if isinstance(expr, UnaryOp):
        if expr.op.upper() == 'NOT':
            return not evaluate_predicate(expr.operand, row)

    if isinstance(expr, IsNullExpr):
        val = evaluate_expression(expr.expr, row)
        if expr.negated:
            return val is not None
        return val is None

    if isinstance(expr, BetweenExpr):
        val = evaluate_expression(expr.expr, row)
        low = evaluate_expression(expr.low, row)
        high = evaluate_expression(expr.high, row)
        if val is None or low is None or high is None:
            return False
        return low <= val <= high

    if isinstance(expr, InExpr):
        val = evaluate_expression(expr.expr, row)
        if val is None:
            return False
        for v_expr in expr.values:
            v = evaluate_expression(v_expr, row)
            if v == val:
                return True
        return False

    if isinstance(expr, Literal):
        return bool(expr.value)

    # Default
    val = evaluate_expression(expr, row)
    return bool(val) if val is not None else False
