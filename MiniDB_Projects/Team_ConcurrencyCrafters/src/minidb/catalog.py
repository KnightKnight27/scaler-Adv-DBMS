from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

from .types import Column, TableSchema, TableStats


@dataclass(slots=True)
class IndexMetadata:
    name: str
    column_name: str
    path: str
    unique: bool
    primary: bool = False


@dataclass(slots=True)
class TableMetadata:
    schema: TableSchema
    heap_file: str
    indexes: dict[str, IndexMetadata] = field(default_factory=dict)
    stats: TableStats = field(default_factory=TableStats)


class Catalog:
    def __init__(self, catalog_path: str | Path):
        self.catalog_path = Path(catalog_path)
        self.catalog_path.parent.mkdir(parents=True, exist_ok=True)
        self.tables: dict[str, TableMetadata] = {}
        self._load()

    def create_table(self, schema: TableSchema, heap_file: str) -> TableMetadata:
        if schema.name in self.tables:
            raise ValueError(f"Table '{schema.name}' already exists.")
        self.tables[schema.name] = TableMetadata(schema=schema, heap_file=heap_file)
        self.save()
        return self.tables[schema.name]

    def create_index(
        self,
        table_name: str,
        index_name: str,
        column_name: str,
        path: str,
        *,
        unique: bool,
        primary: bool = False,
    ) -> IndexMetadata:
        metadata = self.get_table(table_name)
        if index_name in metadata.indexes:
            raise ValueError(f"Index '{index_name}' already exists on '{table_name}'.")
        metadata.get_column(column_name).indexed = True
        index = IndexMetadata(
            name=index_name,
            column_name=column_name,
            path=path,
            unique=unique,
            primary=primary,
        )
        metadata.indexes[index_name] = index
        self.save()
        return index

    def get_table(self, table_name: str) -> TableMetadata:
        if table_name not in self.tables:
            raise KeyError(f"Unknown table '{table_name}'.")
        return self.tables[table_name]

    def update_stats(self, table_name: str, stats: TableStats) -> None:
        metadata = self.get_table(table_name)
        metadata.stats = stats
        self.save()

    def get_index_for_column(self, table_name: str, column_name: str) -> IndexMetadata | None:
        table = self.get_table(table_name)
        for index in table.indexes.values():
            if index.column_name == column_name:
                return index
        return None

    def save(self) -> None:
        payload = {"tables": {}}
        for table_name, metadata in self.tables.items():
            payload["tables"][table_name] = {
                "schema": {
                    "name": metadata.schema.name,
                    "columns": [
                        {
                            "name": column.name,
                            "data_type": column.data_type,
                            "primary_key": column.primary_key,
                            "indexed": column.indexed,
                        }
                        for column in metadata.schema.columns
                    ],
                },
                "heap_file": metadata.heap_file,
                "indexes": {
                    index_name: {
                        "name": index.name,
                        "column_name": index.column_name,
                        "path": index.path,
                        "unique": index.unique,
                        "primary": index.primary,
                    }
                    for index_name, index in metadata.indexes.items()
                },
                "stats": {
                    "row_count": metadata.stats.row_count,
                    "page_count": metadata.stats.page_count,
                },
            }
        self.catalog_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    def _load(self) -> None:
        if not self.catalog_path.exists():
            self.save()
            return
        content = self.catalog_path.read_text(encoding="utf-8").strip()
        if not content:
            self.save()
            return
        data = json.loads(content)
        for table_name, payload in data.get("tables", {}).items():
            columns = [
                Column(
                    name=column["name"],
                    data_type=column["data_type"],
                    primary_key=column.get("primary_key", False),
                    indexed=column.get("indexed", False),
                )
                for column in payload["schema"]["columns"]
            ]
            schema = TableSchema(name=payload["schema"]["name"], columns=columns)
            indexes = {
                index_name: IndexMetadata(
                    name=index_payload["name"],
                    column_name=index_payload["column_name"],
                    path=index_payload["path"],
                    unique=index_payload["unique"],
                    primary=index_payload.get("primary", False),
                )
                for index_name, index_payload in payload.get("indexes", {}).items()
            }
            stats_payload = payload.get("stats", {})
            self.tables[table_name] = TableMetadata(
                schema=schema,
                heap_file=payload["heap_file"],
                indexes=indexes,
                stats=TableStats(
                    row_count=stats_payload.get("row_count", 0),
                    page_count=stats_payload.get("page_count", 0),
                ),
            )


def _metadata_get_column(self: TableMetadata, column_name: str):
    return self.schema.get_column(column_name)


TableMetadata.get_column = _metadata_get_column  # type: ignore[attr-defined]

