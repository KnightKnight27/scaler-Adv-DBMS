"""catalog.py — the system catalog: tables, schemas, indexes, and statistics.

A `Table` bundles everything the rest of the engine needs to operate on one
relation:
  * its `schema` (column definitions),
  * a `HeapFile` holding the rows,
  * a dict of B+ tree `indexes` keyed by column name (the primary key always has
    one), kept in sync on every insert/delete,
  * `stats` (row_count + per-indexed-column distinct counts) for the optimizer.

The `Catalog` is an in-memory registry of tables. Durability is provided by the
WAL (see wal.py): on startup the engine replays committed transactions, calling
the same `insert_row`/`delete_by_key` paths, so indexes and stats are rebuilt
correctly without any catalog-on-disk format.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Iterator

from .btree import BPlusTree, DuplicateKeyError
from .buffer_pool import BufferPool
from .heap import RID, HeapFile
from .types import Schema

# Sentinel larger than any real (page_id, slot) for composite range upper bounds.
_HI = float("inf")


@dataclass
class TableStats:
    row_count: int = 0
    # distinct values per indexed column (for selectivity estimation)
    ndv: dict[str, int] = field(default_factory=dict)


class _Index:
    """A B+ tree index on one column.

    * A unique (primary-key) index stores key = column value -> RID.
    * A non-unique (secondary) index would collapse duplicates, so it stores a
      COMPOSITE key (value, page_id, slot) -> RID. Every entry is then distinct,
      duplicates coexist, and an equality lookup is a range over the value prefix.
    """

    def __init__(self, column: str, unique: bool) -> None:
        self.column = column
        self.unique = unique
        self.tree = BPlusTree(unique=True)  # composite keys are always unique

    def _key(self, value, rid: RID):
        return value if self.unique else (value, rid.page_id, rid.slot)

    def insert(self, value, rid: RID) -> None:
        self.tree.insert(self._key(value, rid), rid)

    def delete(self, value, rid: RID) -> None:
        self.tree.delete(self._key(value, rid))

    def contains(self, value) -> bool:
        # only meaningful for unique indexes (PK uniqueness check)
        return value in self.tree

    def eq(self, value) -> list[RID]:
        if self.unique:
            r = self.tree.search(value)
            return [r] if r is not None else []
        return [rid for _, rid in self.tree.range((value,), (value, _HI, _HI))]

    def range(self, lo, hi) -> list[RID]:
        if self.unique:
            return [rid for _, rid in self.tree.range(lo, hi)]
        lo_key = None if lo is None else (lo,)
        hi_key = None if hi is None else (hi, _HI, _HI)
        return [rid for _, rid in self.tree.range(lo_key, hi_key)]

    def distinct_values(self) -> int:
        if self.unique:
            return sum(1 for _ in self.tree.items())
        return len({k[0] for k, _ in self.tree.items()})  # k = (value, page, slot)


class Table:
    def __init__(self, name: str, schema: Schema, buffer_pool: BufferPool) -> None:
        self.name = name
        self.schema = schema
        self.heap = HeapFile(buffer_pool)
        self.indexes: dict[str, _Index] = {}
        self.stats = TableStats()
        # the primary key column (if any) always gets a unique index
        pk_idx = schema.primary_key_index()
        if pk_idx is not None:
            self.pk_column: str | None = schema.columns[pk_idx].name
            self.indexes[self.pk_column] = _Index(self.pk_column, unique=True)
        else:
            self.pk_column = None

    # --- mutations (keep heap + indexes + stats consistent) ---------------

    def insert_row(self, values: tuple[Any, ...]) -> RID:
        """Insert a row (tuple of Python values); update heap, indexes, stats."""
        # PK uniqueness check before any mutation
        if self.pk_column is not None:
            key = values[self.schema.index_of(self.pk_column)]
            if self.indexes[self.pk_column].contains(key):
                raise DuplicateKeyError(
                    f"duplicate primary key {key!r} in table {self.name!r}"
                )
        record = self.schema.encode(values)
        rid = self.heap.insert(record)
        for col, index in self.indexes.items():
            index.insert(values[self.schema.index_of(col)], rid)
        self.stats.row_count += 1
        return rid

    def delete_by_rid(self, rid: RID, values: tuple[Any, ...]) -> bool:
        """Delete a known row (its RID + decoded values) from heap + indexes."""
        ok = self.heap.delete(rid)
        if not ok:
            return False
        for col, index in self.indexes.items():
            index.delete(values[self.schema.index_of(col)], rid)
        self.stats.row_count -= 1
        return True

    def delete_by_values(self, values: tuple[Any, ...]) -> bool:
        """Delete the first row equal to `values` (used by WAL replay)."""
        for rid, row in self.scan():
            if row == values:
                return self.delete_by_rid(rid, row)
        return False

    def delete_by_key(self, pk_value: Any) -> bool:
        """Delete the row with the given primary key (used by WAL replay)."""
        if self.pk_column is None:
            raise ValueError(f"table {self.name!r} has no primary key to delete by")
        hit = self.get_by_pk(pk_value)
        if hit is None:
            return False
        rid, values = hit
        return self.delete_by_rid(rid, values)

    def index_lookup(self, column: str, value: Any) -> list[RID]:
        """RIDs of all rows where column == value, using the column's index."""
        return self.indexes[column].eq(value)

    def index_range(self, column: str, lo: Any, hi: Any) -> list[RID]:
        """RIDs of all rows where lo <= column <= hi, using the column's index."""
        return self.indexes[column].range(lo, hi)

    # --- reads -------------------------------------------------------------

    def get_by_pk(self, pk_value: Any) -> tuple[RID, tuple[Any, ...]] | None:
        if self.pk_column is None:
            return None
        rids = self.indexes[self.pk_column].eq(pk_value)
        if not rids:
            return None
        rec = self.heap.get(rids[0])
        return (rids[0], self.schema.decode(rec)) if rec is not None else None

    def scan(self) -> Iterator[tuple[RID, tuple[Any, ...]]]:
        """Sequential scan: yield (RID, decoded row) for every live row."""
        for rid, record in self.heap.scan():
            yield rid, self.schema.decode(record)

    # --- indexing & stats --------------------------------------------------

    def create_index(self, column: str, unique: bool = False) -> None:
        """Build a secondary index on `column` from the existing rows."""
        if column in self.indexes:
            return
        col_pos = self.schema.index_of(column)  # validates column exists
        index = _Index(column, unique=unique)
        for rid, values in self.scan():
            index.insert(values[col_pos], rid)
        self.indexes[column] = index
        self.analyze()

    def has_index(self, column: str) -> bool:
        return column in self.indexes

    def analyze(self) -> None:
        """Recompute statistics (row_count + distinct counts on indexed cols)."""
        self.stats.row_count = len(self.heap)
        for col, index in self.indexes.items():
            self.stats.ndv[col] = index.distinct_values()


class Catalog:
    def __init__(self, buffer_pool: BufferPool) -> None:
        self.bp = buffer_pool
        self.tables: dict[str, Table] = {}

    def create_table(self, name: str, schema: Schema) -> Table:
        if name in self.tables:
            raise ValueError(f"table {name!r} already exists")
        table = Table(name, schema, self.bp)
        self.tables[name] = table
        return table

    def get_table(self, name: str) -> Table:
        if name not in self.tables:
            raise KeyError(f"no such table: {name!r}")
        return self.tables[name]

    def has_table(self, name: str) -> bool:
        return name in self.tables

    def drop_table(self, name: str) -> None:
        if name not in self.tables:
            raise KeyError(f"no such table: {name!r}")
        del self.tables[name]

    def table_names(self) -> list[str]:
        return sorted(self.tables)
