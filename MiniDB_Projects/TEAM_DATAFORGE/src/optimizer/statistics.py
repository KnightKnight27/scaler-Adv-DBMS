"""
Table Statistics — Stats collection for the cost-based optimizer.

Maintains cardinality, distinct value counts, min/max ranges, and
simple histograms per column. Used by the CostEstimator for
selectivity estimation and scan-choice decisions.
"""

from typing import Optional
from src.catalog.catalog import Catalog, TableStats


class TableStatistics:
    """
    Collects and manages statistics about tables for the optimizer.

    Call analyze_table() to scan a table and gather stats.
    Stats are persisted in the catalog.
    """

    def __init__(self, catalog: Catalog):
        self.catalog = catalog

    def analyze_table(self, table_name: str, heap_file) -> TableStats:
        """
        Scan a table and compute statistics.

        Args:
            table_name: Name of the table.
            heap_file: The HeapFile object to scan.

        Returns:
            Updated TableStats.
        """
        table_info = self.catalog.get_table(table_name)
        if table_info is None:
            raise ValueError(f"Table '{table_name}' not found in catalog")

        col_names = table_info.get_column_names()
        num_cols = len(col_names)

        # Initialize tracking structures
        distinct_sets = {col: set() for col in col_names}
        min_vals = {col: None for col in col_names}
        max_vals = {col: None for col in col_names}
        row_count = 0

        # Scan all records
        for rid, values in heap_file.scan():
            row_count += 1
            for i, val in enumerate(values):
                if i < num_cols and val is not None:
                    col = col_names[i]
                    distinct_sets[col].add(val)

                    if min_vals[col] is None or val < min_vals[col]:
                        min_vals[col] = val
                    if max_vals[col] is None or val > max_vals[col]:
                        max_vals[col] = val

        stats = TableStats(
            row_count=row_count,
            page_count=heap_file.page_count(),
            distinct_values={col: len(s) for col, s in distinct_sets.items()},
            min_values={col: v for col, v in min_vals.items() if v is not None},
            max_values={col: v for col, v in max_vals.items() if v is not None},
        )

        # Persist to catalog
        self.catalog.update_stats(table_name, stats)
        return stats

    def get_row_count(self, table_name: str) -> int:
        """Get the estimated row count for a table."""
        table_info = self.catalog.get_table(table_name)
        if table_info and table_info.stats:
            return table_info.stats.row_count
        return 1000  # Default estimate

    def get_distinct_count(self, table_name: str, column_name: str) -> int:
        """Get the distinct value count for a column."""
        table_info = self.catalog.get_table(table_name)
        if table_info and table_info.stats:
            return table_info.stats.distinct_values.get(column_name, 10)
        return 10  # Default estimate

    def get_min_max(self, table_name: str, column_name: str) -> tuple:
        """Get (min, max) for a column."""
        table_info = self.catalog.get_table(table_name)
        if table_info and table_info.stats:
            mn = table_info.stats.min_values.get(column_name)
            mx = table_info.stats.max_values.get(column_name)
            return (mn, mx)
        return (None, None)
