"""
Cost-based query optimizer.

Responsibilities:
  1. Choose between SeqScan and IndexScan for each table access.
  2. Estimate selectivity for WHERE predicates.
  3. Order joins: smallest estimated output first (greedy).

Cost model (simplified):
  - SeqScan cost  = num_pages
  - IndexScan cost = log2(num_rows) + selectivity * num_rows
  - Selectivity   = 1 / ndistinct  for equality predicates
                  = 0.3            for range predicates
"""
import math


class TableStats:
    def __init__(self, num_rows: int = 0, num_pages: int = 1):
        self.num_rows = num_rows
        self.num_pages = num_pages
        self.col_ndistinct: dict[str, int] = {}  # col -> approx distinct count

    def update(self, col: str, ndistinct: int):
        self.col_ndistinct[col] = ndistinct

    def selectivity(self, col: str, op: str) -> float:
        """Estimate fraction of rows matching col op val."""
        if op == '=':
            nd = self.col_ndistinct.get(col, max(1, self.num_rows // 10))
            return 1.0 / max(1, nd)
        if op in ('<', '>', '<=', '>='):
            return 0.3
        if op == '!=':
            nd = self.col_ndistinct.get(col, max(1, self.num_rows // 10))
            return 1.0 - (1.0 / max(1, nd))
        return 0.5

    def estimated_output(self, conditions: list) -> int:
        """Estimate rows after applying AND conditions."""
        sel = 1.0
        for cond in conditions:
            col = cond.left.split('.')[-1]  # strip table prefix
            sel *= self.selectivity(col, cond.op)
        return max(1, int(self.num_rows * sel))

    def seq_scan_cost(self) -> float:
        return float(self.num_pages)

    def index_scan_cost(self, selectivity: float) -> float:
        if self.num_rows == 0:
            return 0.0
        return math.log2(max(2, self.num_rows)) + selectivity * self.num_rows


class QueryPlan:
    """Describes how to execute a query."""
    def __init__(self):
        self.table_scans: dict[str, str] = {}  # table -> 'seq' | 'index'
        self.index_cols: dict[str, str] = {}   # table -> column to use for index
        self.join_order: list[str] = []         # tables in join order


class Optimizer:
    def __init__(self):
        self.stats: dict[str, TableStats] = {}  # table name -> TableStats

    def get_stats(self, table: str) -> TableStats:
        return self.stats.get(table, TableStats())

    def update_stats(self, table: str, stats: TableStats):
        self.stats[table] = stats

    def optimize(self, stmt, indexes: dict) -> QueryPlan:
        """
        stmt: SelectStmt from parser
        indexes: {table_name: set_of_indexed_columns}
        Returns a QueryPlan.
        """
        from sql.parser import SelectStmt, Condition
        plan = QueryPlan()

        # collect all tables
        tables = [stmt.table]
        for j in stmt.joins:
            tables.append(j.table)

        # decide scan type for each table
        where_by_table = self._group_conditions(stmt.where, tables)

        for table in tables:
            stats = self.get_stats(table)
            table_indexes = indexes.get(table, set())
            conds = where_by_table.get(table, [])

            best_col = None
            best_sel = 1.0

            for cond in conds:
                col = cond.left.split('.')[-1]
                if col in table_indexes:
                    sel = stats.selectivity(col, cond.op)
                    if sel < best_sel:
                        best_sel = sel
                        best_col = col

            if best_col and stats.index_scan_cost(best_sel) < stats.seq_scan_cost():
                plan.table_scans[table] = 'index'
                plan.index_cols[table] = best_col
            else:
                plan.table_scans[table] = 'seq'

        # join order: sort by estimated output size (smallest first)
        def est_size(t):
            conds = where_by_table.get(t, [])
            return self.get_stats(t).estimated_output(conds)

        plan.join_order = sorted(tables, key=est_size)
        return plan

    def _group_conditions(self, conditions: list, tables: list) -> dict:
        """Group WHERE conditions by the table they reference."""
        result = {t: [] for t in tables}
        for cond in conditions:
            col = cond.left
            if '.' in col:
                tbl, _ = col.split('.', 1)
                if tbl in result:
                    result[tbl].append(cond)
            else:
                # assign to first table that has this column (approx)
                for t in tables:
                    result[t].append(cond)
                    break
        return result
