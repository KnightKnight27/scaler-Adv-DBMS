"""
System Catalog — Metadata store for MiniDB.

Tracks all tables, columns, indexes, and table statistics.
The catalog is kept in memory and persisted to a JSON file.
"""

import json
import os
from dataclasses import dataclass, field, asdict
from typing import Optional


@dataclass
class ColumnInfo:
    """Metadata for a single column."""
    name: str
    data_type: str          # 'INTEGER', 'FLOAT', 'VARCHAR', 'BOOLEAN', 'TEXT'
    is_primary_key: bool = False
    is_nullable: bool = True
    max_length: int = 255   # For VARCHAR

    def __post_init__(self):
        self.data_type = self.data_type.upper()


@dataclass
class IndexInfo:
    """Metadata for an index."""
    name: str
    table_name: str
    column_name: str
    is_primary: bool = False
    index_type: str = 'BTREE'


@dataclass
class TableStats:
    """Statistics for cost-based optimization."""
    row_count: int = 0
    page_count: int = 0
    distinct_values: dict = field(default_factory=dict)  # column_name -> count
    min_values: dict = field(default_factory=dict)        # column_name -> min
    max_values: dict = field(default_factory=dict)        # column_name -> max


@dataclass
class TableInfo:
    """Complete metadata for a table."""
    name: str
    columns: list  # List of ColumnInfo
    indexes: list = field(default_factory=list)   # List of IndexInfo
    stats: TableStats = field(default_factory=TableStats)
    primary_key: str = None  # Column name of primary key

    def get_column(self, name: str) -> Optional[ColumnInfo]:
        """Get column info by name (case-insensitive)."""
        name_lower = name.lower()
        for col in self.columns:
            if col.name.lower() == name_lower:
                return col
        return None

    def get_column_index(self, name: str) -> int:
        """Get the index position of a column by name."""
        name_lower = name.lower()
        for i, col in enumerate(self.columns):
            if col.name.lower() == name_lower:
                return i
        return -1

    def get_column_types(self) -> list:
        """Get list of column type strings."""
        return [col.data_type for col in self.columns]

    def get_column_names(self) -> list:
        """Get list of column names."""
        return [col.name for col in self.columns]

    def has_index(self, column_name: str) -> bool:
        """Check if a column has an index."""
        col_lower = column_name.lower()
        return any(idx.column_name.lower() == col_lower for idx in self.indexes)


class Catalog:
    """
    System catalog — stores and manages metadata for all database objects.

    The catalog is persisted to a JSON file so metadata survives restarts.

    Usage:
        catalog = Catalog('/path/to/db')
        catalog.create_table('employees', [
            ColumnInfo('id', 'INTEGER', is_primary_key=True),
            ColumnInfo('name', 'VARCHAR'),
            ColumnInfo('salary', 'FLOAT'),
        ])
        table_info = catalog.get_table('employees')
    """

    def __init__(self, db_dir: str):
        """
        Initialize the catalog.

        Args:
            db_dir: Directory where the catalog file is stored.
        """
        self.db_dir = db_dir
        self._tables: dict[str, TableInfo] = {}
        self._catalog_file = os.path.join(db_dir, '_catalog.json')

        os.makedirs(db_dir, exist_ok=True)
        self._load()

    def _load(self):
        """Load catalog from disk."""
        if os.path.exists(self._catalog_file):
            try:
                with open(self._catalog_file, 'r') as f:
                    data = json.load(f)
                for tname, tdata in data.get('tables', {}).items():
                    columns = [ColumnInfo(**c) for c in tdata['columns']]
                    indexes = [IndexInfo(**i) for i in tdata.get('indexes', [])]
                    stats_data = tdata.get('stats', {})
                    stats = TableStats(
                        row_count=stats_data.get('row_count', 0),
                        page_count=stats_data.get('page_count', 0),
                        distinct_values=stats_data.get('distinct_values', {}),
                        min_values=stats_data.get('min_values', {}),
                        max_values=stats_data.get('max_values', {}),
                    )
                    self._tables[tname] = TableInfo(
                        name=tname,
                        columns=columns,
                        indexes=indexes,
                        stats=stats,
                        primary_key=tdata.get('primary_key'),
                    )
            except (json.JSONDecodeError, KeyError):
                self._tables = {}

    def _save(self):
        """Persist catalog to disk."""
        data = {'tables': {}}
        for tname, tinfo in self._tables.items():
            data['tables'][tname] = {
                'columns': [asdict(c) for c in tinfo.columns],
                'indexes': [asdict(i) for i in tinfo.indexes],
                'stats': asdict(tinfo.stats),
                'primary_key': tinfo.primary_key,
            }
        with open(self._catalog_file, 'w') as f:
            json.dump(data, f, indent=2, default=str)

    def create_table(self, name: str, columns: list, primary_key: str = None) -> TableInfo:
        """
        Register a new table in the catalog.

        Args:
            name: Table name.
            columns: List of ColumnInfo objects.
            primary_key: Name of the primary key column.

        Returns:
            The created TableInfo.

        Raises:
            ValueError: If table already exists.
        """
        name_lower = name.lower()
        if name_lower in self._tables:
            raise ValueError(f"Table '{name}' already exists")

        # Auto-detect primary key if not specified
        if primary_key is None:
            for col in columns:
                if col.is_primary_key:
                    primary_key = col.name
                    break

        table_info = TableInfo(
            name=name_lower,
            columns=columns,
            primary_key=primary_key,
        )

        # Auto-create primary key index
        if primary_key:
            idx = IndexInfo(
                name=f"idx_{name_lower}_{primary_key.lower()}",
                table_name=name_lower,
                column_name=primary_key.lower(),
                is_primary=True,
            )
            table_info.indexes.append(idx)

        self._tables[name_lower] = table_info
        self._save()
        return table_info

    def drop_table(self, name: str):
        """Remove a table from the catalog."""
        name_lower = name.lower()
        if name_lower not in self._tables:
            raise ValueError(f"Table '{name}' does not exist")
        del self._tables[name_lower]
        self._save()

    def get_table(self, name: str) -> Optional[TableInfo]:
        """Get table metadata by name."""
        return self._tables.get(name.lower())

    def table_exists(self, name: str) -> bool:
        """Check if a table exists."""
        return name.lower() in self._tables

    def list_tables(self) -> list:
        """List all table names."""
        return list(self._tables.keys())

    def create_index(self, index_name: str, table_name: str, column_name: str) -> IndexInfo:
        """
        Register a new index in the catalog.

        Args:
            index_name: Name of the index.
            table_name: Table the index is on.
            column_name: Column the index is on.

        Returns:
            The created IndexInfo.
        """
        table = self.get_table(table_name)
        if table is None:
            raise ValueError(f"Table '{table_name}' does not exist")

        col = table.get_column(column_name)
        if col is None:
            raise ValueError(f"Column '{column_name}' does not exist in '{table_name}'")

        idx = IndexInfo(
            name=index_name.lower(),
            table_name=table_name.lower(),
            column_name=column_name.lower(),
        )
        table.indexes.append(idx)
        self._save()
        return idx

    def update_stats(self, table_name: str, stats: TableStats):
        """Update statistics for a table."""
        table = self.get_table(table_name)
        if table:
            table.stats = stats
            self._save()

    def get_all_tables(self) -> dict:
        """Get all table infos."""
        return dict(self._tables)
