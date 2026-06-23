"""
Catalog: per-table metadata, index registry, and statistics.

Statistics feed the cost-based optimizer:
  * n_tuples           -- table cardinality
  * column ndv         -- number of distinct values (for equality selectivity)
  * column min/max     -- for range selectivity
These are refreshed by analyze() and after bulk loads.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

from .btree import BPlusTree
from .types import DataType, Schema


@dataclass
class ColumnStats:
    ndv: int = 1
    min: Any = None
    max: Any = None


@dataclass
class TableInfo:
    name: str
    schema: Schema
    primary_key: Optional[str]
    file_key: str
    indexes: Dict[str, BPlusTree] = field(default_factory=dict)  # column -> tree
    n_tuples: int = 0
    col_stats: Dict[str, ColumnStats] = field(default_factory=dict)

    def has_index(self, column: str) -> bool:
        return column in self.indexes


class Catalog:
    def __init__(self):
        self.tables: Dict[str, TableInfo] = {}

    def add_table(self, info: TableInfo):
        self.tables[info.name] = info

    def get(self, name: str) -> TableInfo:
        if name not in self.tables:
            raise KeyError(f"no such table: {name}")
        return self.tables[name]

    def exists(self, name: str) -> bool:
        return name in self.tables

    def analyze(self, name: str, heap, deserialize):
        """Recompute statistics by scanning the heap file."""
        info = self.tables[name]
        n = 0
        distinct: Dict[str, set] = {c.name: set() for c in info.schema.columns}
        mins: Dict[str, Any] = {}
        maxs: Dict[str, Any] = {}
        for _rid, raw in heap.scan():
            row = deserialize(raw)
            n += 1
            for col, val in zip(info.schema.columns, row):
                if val is None:
                    continue
                distinct[col.name].add(val)
                if col.type in (DataType.INT, DataType.FLOAT):
                    if col.name not in mins or val < mins[col.name]:
                        mins[col.name] = val
                    if col.name not in maxs or val > maxs[col.name]:
                        maxs[col.name] = val
        info.n_tuples = n
        info.col_stats = {
            c.name: ColumnStats(
                ndv=max(1, len(distinct[c.name])),
                min=mins.get(c.name),
                max=maxs.get(c.name),
            )
            for c in info.schema.columns
        }
