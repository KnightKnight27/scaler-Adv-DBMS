"""
Cost Estimator — Selectivity and cost estimation for MiniDB's optimizer.

Estimates:
  - Selectivity of predicates (equality, range, AND, OR)
  - I/O cost of table scans vs index scans
  - Join costs for different join methods

The estimates drive the optimizer's choice of physical plan.
"""

from typing import Optional
from src.catalog.catalog import Catalog, TableInfo
from src.parser.ast_nodes import *


# Cost constants (in arbitrary "cost units" roughly representing I/O)
SEQ_SCAN_COST_PER_PAGE = 1.0
INDEX_SCAN_COST_PER_PAGE = 1.5    # Random I/O is more expensive
CPU_COST_PER_TUPLE = 0.01
NESTED_LOOP_FACTOR = 1.0
HASH_JOIN_BUILD_FACTOR = 2.0


class CostEstimator:
    """
    Estimates selectivity and cost for query plan decisions.

    Usage:
        estimator = CostEstimator(catalog)
        sel = estimator.estimate_selectivity('employees', where_expr)
        cost = estimator.estimate_seq_scan_cost('employees')
    """

    def __init__(self, catalog: Catalog):
        self.catalog = catalog

    # ─── Selectivity Estimation ──────────────────────────────────────

    def estimate_selectivity(self, table_name: str, expr: Expression,
                             table_alias: str = None) -> float:
        """
        Estimate the selectivity of a predicate expression.

        Selectivity is a float in [0, 1] representing the fraction of
        rows that satisfy the predicate.

        Args:
            table_name: Name of the table.
            expr: The WHERE predicate expression.
            table_alias: Optional alias for the table.

        Returns:
            Estimated selectivity (0.0 to 1.0).
        """
        if expr is None:
            return 1.0

        table_info = self.catalog.get_table(table_name)
        if table_info is None:
            return 0.5  # Default

        if isinstance(expr, BinaryOp):
            return self._selectivity_binary(table_info, expr, table_alias)
        elif isinstance(expr, IsNullExpr):
            return 0.01 if not expr.negated else 0.99
        elif isinstance(expr, BetweenExpr):
            return self._selectivity_range(table_info, expr)
        elif isinstance(expr, InExpr):
            n = len(expr.values)
            col_name = self._get_column_name(expr.expr)
            if col_name:
                distinct = self._get_distinct(table_info, col_name)
                return min(1.0, n / distinct)
            return min(1.0, n * 0.1)
        elif isinstance(expr, UnaryOp) and expr.op == 'NOT':
            return 1.0 - self.estimate_selectivity(table_name, expr.operand, table_alias)

        return 0.5  # Default for unknown expressions

    def _selectivity_binary(self, table_info: TableInfo, expr: BinaryOp,
                            table_alias: str = None) -> float:
        """Estimate selectivity for binary operations."""
        op = expr.op.upper()

        # AND: multiply selectivities (independence assumption)
        if op == 'AND':
            s1 = self.estimate_selectivity(table_info.name, expr.left, table_alias)
            s2 = self.estimate_selectivity(table_info.name, expr.right, table_alias)
            return s1 * s2

        # OR: 1 - (1-s1)(1-s2)
        if op == 'OR':
            s1 = self.estimate_selectivity(table_info.name, expr.left, table_alias)
            s2 = self.estimate_selectivity(table_info.name, expr.right, table_alias)
            return s1 + s2 - s1 * s2

        # Comparison operators
        col_name = self._get_column_name(expr.left)
        if col_name is None:
            col_name = self._get_column_name(expr.right)

        if col_name is None:
            return 0.5

        if op in ('=', '=='):
            # Equality: 1 / distinct_values
            distinct = self._get_distinct(table_info, col_name)
            return 1.0 / distinct

        if op in ('!=', '<>'):
            distinct = self._get_distinct(table_info, col_name)
            return 1.0 - (1.0 / distinct)

        if op in ('<', '>', '<=', '>='):
            # Range: use min/max if available
            return self._selectivity_comparison(table_info, col_name, op, expr)

        return 0.5

    def _selectivity_comparison(self, table_info: TableInfo, col_name: str,
                                 op: str, expr: BinaryOp) -> float:
        """Estimate selectivity for comparison operators using value ranges."""
        stats = table_info.stats
        if not stats:
            return 0.33  # Default for range predicates

        mn = stats.min_values.get(col_name)
        mx = stats.max_values.get(col_name)

        if mn is None or mx is None or mn == mx:
            return 0.33

        # Try to get the literal value
        literal_val = self._get_literal_value(expr.right)
        if literal_val is None:
            literal_val = self._get_literal_value(expr.left)
        if literal_val is None:
            return 0.33

        try:
            if isinstance(mn, (int, float)) and isinstance(literal_val, (int, float)):
                range_size = float(mx - mn)
                if range_size <= 0:
                    return 0.33

                if op in ('<', '<='):
                    return max(0.0, min(1.0, (literal_val - mn) / range_size))
                elif op in ('>', '>='):
                    return max(0.0, min(1.0, (mx - literal_val) / range_size))
        except (TypeError, ValueError):
            pass

        return 0.33

    def _selectivity_range(self, table_info: TableInfo, expr: BetweenExpr) -> float:
        """Estimate selectivity for BETWEEN."""
        col_name = self._get_column_name(expr.expr)
        if col_name is None:
            return 0.25

        stats = table_info.stats
        if not stats:
            return 0.25

        mn = stats.min_values.get(col_name)
        mx = stats.max_values.get(col_name)
        if mn is None or mx is None or mn == mx:
            return 0.25

        low = self._get_literal_value(expr.low)
        high = self._get_literal_value(expr.high)
        if low is None or high is None:
            return 0.25

        try:
            range_size = float(mx - mn)
            if range_size <= 0:
                return 0.25
            return max(0.0, min(1.0, (high - low) / range_size))
        except (TypeError, ValueError):
            return 0.25

    def _get_distinct(self, table_info: TableInfo, col_name: str) -> float:
        """Get distinct count for a column, with defaults."""
        if table_info.stats and col_name in table_info.stats.distinct_values:
            d = table_info.stats.distinct_values[col_name]
            return max(1, d)
        return max(1, table_info.stats.row_count // 10) if table_info.stats else 10

    def _get_column_name(self, expr: Expression) -> Optional[str]:
        """Extract column name from an expression."""
        if isinstance(expr, ColumnRef):
            return expr.column
        return None

    def _get_literal_value(self, expr: Expression):
        """Extract literal value from an expression."""
        if isinstance(expr, Literal):
            return expr.value
        return None

    # ─── Cost Estimation ─────────────────────────────────────────────

    def estimate_seq_scan_cost(self, table_name: str) -> float:
        """
        Estimate cost of a sequential (full table) scan.

        Cost = pages * SEQ_SCAN_COST + rows * CPU_COST
        """
        table_info = self.catalog.get_table(table_name)
        if table_info is None:
            return 1000.0

        pages = max(1, table_info.stats.page_count) if table_info.stats else 10
        rows = max(1, table_info.stats.row_count) if table_info.stats else 1000

        return pages * SEQ_SCAN_COST_PER_PAGE + rows * CPU_COST_PER_TUPLE

    def estimate_index_scan_cost(self, table_name: str, selectivity: float) -> float:
        """
        Estimate cost of an index scan.

        Cost = selectivity * pages * INDEX_SCAN_COST + matching_rows * CPU_COST
        """
        table_info = self.catalog.get_table(table_name)
        if table_info is None:
            return 500.0

        pages = max(1, table_info.stats.page_count) if table_info.stats else 10
        rows = max(1, table_info.stats.row_count) if table_info.stats else 1000

        # Index scan cost: B+ tree lookup + random page I/O for matching records
        matching_rows = int(rows * selectivity)
        # Approximate matching pages (assume uniform distribution)
        matching_pages = max(1, int(pages * selectivity))

        # Tree traversal cost (log)
        import math
        tree_cost = math.log2(max(2, rows)) * INDEX_SCAN_COST_PER_PAGE

        return tree_cost + matching_pages * INDEX_SCAN_COST_PER_PAGE + matching_rows * CPU_COST_PER_TUPLE

    def estimate_nested_loop_join_cost(self, outer_table: str, inner_table: str,
                                        outer_selectivity: float = 1.0) -> float:
        """
        Estimate cost of a nested loop join.

        Cost = outer_pages + (outer_rows * inner_pages)
        """
        outer_info = self.catalog.get_table(outer_table)
        inner_info = self.catalog.get_table(inner_table)

        outer_pages = max(1, outer_info.stats.page_count) if outer_info and outer_info.stats else 10
        outer_rows = max(1, outer_info.stats.row_count) if outer_info and outer_info.stats else 1000
        inner_pages = max(1, inner_info.stats.page_count) if inner_info and inner_info.stats else 10

        effective_outer_rows = int(outer_rows * outer_selectivity)

        return (outer_pages * SEQ_SCAN_COST_PER_PAGE +
                effective_outer_rows * inner_pages * SEQ_SCAN_COST_PER_PAGE)

    def should_use_index(self, table_name: str, column_name: str,
                         selectivity: float) -> bool:
        """
        Decide whether to use an index scan or sequential scan.

        Generally, index scans are better when selectivity < ~15%.

        Returns:
            True if index scan is preferred.
        """
        table_info = self.catalog.get_table(table_name)
        if table_info is None:
            return False

        if not table_info.has_index(column_name):
            return False

        seq_cost = self.estimate_seq_scan_cost(table_name)
        idx_cost = self.estimate_index_scan_cost(table_name, selectivity)

        return idx_cost < seq_cost
