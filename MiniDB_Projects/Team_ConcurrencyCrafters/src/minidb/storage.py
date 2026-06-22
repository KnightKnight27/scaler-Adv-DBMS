from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from .buffer import BufferPoolManager
from .catalog import Catalog, IndexMetadata, TableMetadata
from .index import PersistentBPlusTree
from .pages import PageManager
from .types import RecordID, StorageEngine, TableSchema, TableStats, Value


class HeapFile:
    def __init__(self, page_manager: PageManager, buffer_pool: BufferPoolManager):
        self.page_manager = page_manager
        self.buffer_pool = buffer_pool

    def insert_record(self, payload: bytes) -> RecordID:
        for page_id in range(self.page_manager.page_count):
            page = self.buffer_pool.fetch_page(page_id)
            try:
                if page.can_fit(len(payload)):
                    slot_id = page.insert(payload)
                    self.buffer_pool.unpin_page(page_id, is_dirty=True)
                    self.buffer_pool.flush_page(page_id)
                    return RecordID(page_id=page_id, slot_id=slot_id)
                self.buffer_pool.unpin_page(page_id, is_dirty=False)
            except Exception:
                self.buffer_pool.unpin_page(page_id, is_dirty=False)
                raise
        page = self.buffer_pool.new_page()
        try:
            slot_id = page.insert(payload)
            self.buffer_pool.unpin_page(page.page_id, is_dirty=True)
            self.buffer_pool.flush_page(page.page_id)
            return RecordID(page_id=page.page_id, slot_id=slot_id)
        except Exception:
            self.buffer_pool.unpin_page(page.page_id, is_dirty=False)
            raise

    def read_record(self, rid: RecordID) -> bytes | None:
        page = self.buffer_pool.fetch_page(rid.page_id)
        try:
            return page.read(rid.slot_id)
        finally:
            self.buffer_pool.unpin_page(rid.page_id, is_dirty=False)

    def delete_record(self, rid: RecordID) -> bytes | None:
        page = self.buffer_pool.fetch_page(rid.page_id)
        try:
            payload = page.read(rid.slot_id)
            if payload is None:
                self.buffer_pool.unpin_page(rid.page_id, is_dirty=False)
                return None
            page.delete(rid.slot_id)
            self.buffer_pool.unpin_page(rid.page_id, is_dirty=True)
            self.buffer_pool.flush_page(rid.page_id)
            return payload
        except Exception:
            self.buffer_pool.unpin_page(rid.page_id, is_dirty=False)
            raise

    def restore_record(self, rid: RecordID, payload: bytes) -> None:
        page = self.buffer_pool.fetch_page(rid.page_id)
        try:
            page.restore(rid.slot_id, payload)
            self.buffer_pool.unpin_page(rid.page_id, is_dirty=True)
            self.buffer_pool.flush_page(rid.page_id)
        except Exception:
            self.buffer_pool.unpin_page(rid.page_id, is_dirty=False)
            raise

    def scan_records(self) -> list[tuple[RecordID, bytes]]:
        rows: list[tuple[RecordID, bytes]] = []
        for page_id in range(self.page_manager.page_count):
            page = self.buffer_pool.fetch_page(page_id)
            try:
                rows.extend(
                    (RecordID(page_id=page_id, slot_id=slot_id), payload)
                    for slot_id, payload in page.iter_records()
                )
            finally:
                self.buffer_pool.unpin_page(page_id, is_dirty=False)
        return rows

    def scan_record_ids(self) -> list[RecordID]:
        row_ids: list[RecordID] = []
        for page_id in range(self.page_manager.page_count):
            page = self.buffer_pool.fetch_page(page_id)
            try:
                row_ids.extend(
                    RecordID(page_id=page_id, slot_id=slot_id)
                    for slot_id in page.iter_record_ids()
                )
            finally:
                self.buffer_pool.unpin_page(page_id, is_dirty=False)
        return row_ids


@dataclass
class TableRuntime:
    metadata: TableMetadata
    page_manager: PageManager
    buffer_pool: BufferPoolManager
    heap_file: HeapFile
    indexes: dict[str, PersistentBPlusTree]

    def index_for_column(self, column_name: str) -> tuple[IndexMetadata, PersistentBPlusTree] | None:
        for index_name, index_metadata in self.metadata.indexes.items():
            if index_metadata.column_name == column_name:
                return index_metadata, self.indexes[index_name]
        return None


class HeapStorageEngine(StorageEngine):
    def __init__(self, base_dir: str | Path, catalog: Catalog, buffer_pool_size: int = 8):
        self.base_dir = Path(base_dir)
        self.base_dir.mkdir(parents=True, exist_ok=True)
        self.catalog = catalog
        self.buffer_pool_size = buffer_pool_size
        self.tables: dict[str, TableRuntime] = {}
        self._load_tables()

    def create_table(self, schema: TableSchema) -> None:
        primary_key = schema.primary_key
        if primary_key is None:
            for column in schema.columns:
                if column.normalized_type() == "INT":
                    column.primary_key = True
                    primary_key = column
                    break
        if primary_key is None or primary_key.normalized_type() != "INT":
            raise ValueError("MiniDB requires an integer primary key column.")
        heap_file = f"{schema.name}.heap"
        self.catalog.create_table(schema, heap_file=heap_file)
        runtime = self._runtime_for_table(schema.name)
        self.tables[schema.name] = runtime
        self.create_index(schema.name, f"{schema.name}_pk", primary_key.name)

    def create_index(self, table_name: str, index_name: str, column_name: str) -> None:
        metadata = self.catalog.get_table(table_name)
        column = metadata.schema.get_column(column_name)
        if column.normalized_type() != "INT":
            raise ValueError("B+ tree indexes currently support INT columns only.")
        primary = column.primary_key
        index_path = f"{index_name}.index.json"
        index_metadata = self.catalog.create_index(
            table_name,
            index_name,
            column_name,
            index_path,
            unique=primary,
            primary=primary,
        )
        runtime = self.tables.get(table_name) or self._runtime_for_table(table_name)
        runtime.indexes[index_name] = PersistentBPlusTree(
            self.base_dir / index_metadata.path,
            unique=index_metadata.unique,
        )
        rows = self.scan(table_name)
        for rid, row in rows:
            runtime.indexes[index_name].insert(int(row[column_name]), rid)
        self.tables[table_name] = runtime
        self._sync_stats(table_name)

    def insert(self, table_name: str, row: dict[str, Value]) -> RecordID:
        runtime = self._require_table(table_name)
        payload = self._serialize_row(row)
        for index_metadata, index in self._iter_indexes(runtime):
            key = int(row[index_metadata.column_name])
            if index_metadata.unique and index.search(key):
                raise ValueError(f"Duplicate key '{key}' on index '{index_metadata.name}'.")
        rid = runtime.heap_file.insert_record(payload)
        for index_metadata, index in self._iter_indexes(runtime):
            key = int(row[index_metadata.column_name])
            index.insert(key, rid)
        self._update_row_count(table_name, delta=1)
        return rid

    def read(self, table_name: str, rid: RecordID) -> dict[str, Value] | None:
        runtime = self._require_table(table_name)
        payload = runtime.heap_file.read_record(rid)
        if payload is None:
            return None
        return self._deserialize_row(payload)

    def delete(self, table_name: str, rid: RecordID) -> dict[str, Value] | None:
        runtime = self._require_table(table_name)
        row = self.read(table_name, rid)
        if row is None:
            return None
        runtime.heap_file.delete_record(rid)
        for index_metadata, index in self._iter_indexes(runtime):
            key = int(row[index_metadata.column_name])
            index.delete(key, rid)
        self._update_row_count(table_name, delta=-1)
        return row

    def restore(self, table_name: str, rid: RecordID, row: dict[str, Value]) -> None:
        runtime = self._require_table(table_name)
        payload = self._serialize_row(row)
        runtime.heap_file.restore_record(rid, payload)
        for index_metadata, index in self._iter_indexes(runtime):
            key = int(row[index_metadata.column_name])
            if rid not in index.search(key):
                index.insert(key, rid)
        self._update_row_count(table_name, delta=1)

    def scan(self, table_name: str) -> list[tuple[RecordID, dict[str, Value]]]:
        runtime = self._require_table(table_name)
        return [
            (rid, self._deserialize_row(payload))
            for rid, payload in runtime.heap_file.scan_records()
        ]

    def scan_record_ids(self, table_name: str) -> list[RecordID]:
        runtime = self._require_table(table_name)
        return runtime.heap_file.scan_record_ids()

    def lookup_index(self, table_name: str, column_name: str, key: int) -> list[RecordID]:
        runtime = self._require_table(table_name)
        index_match = runtime.index_for_column(column_name)
        if index_match is None:
            return []
        _, index = index_match
        return index.search(key)

    def get_stats(self, table_name: str) -> TableStats:
        return self.catalog.get_table(table_name).stats

    def primary_key_column(self, table_name: str) -> str:
        primary_key = self.catalog.get_table(table_name).schema.primary_key
        if primary_key is None:
            raise ValueError(f"Table '{table_name}' does not have a primary key.")
        return primary_key.name

    def _load_tables(self) -> None:
        for table_name in self.catalog.tables:
            self.tables[table_name] = self._runtime_for_table(table_name)

    def _runtime_for_table(self, table_name: str) -> TableRuntime:
        metadata = self.catalog.get_table(table_name)
        page_manager = PageManager(self.base_dir / metadata.heap_file)
        buffer_pool = BufferPoolManager(page_manager=page_manager, pool_size=self.buffer_pool_size)
        indexes = {
            index_name: PersistentBPlusTree(self.base_dir / index.path, unique=index.unique)
            for index_name, index in metadata.indexes.items()
        }
        return TableRuntime(
            metadata=metadata,
            page_manager=page_manager,
            buffer_pool=buffer_pool,
            heap_file=HeapFile(page_manager=page_manager, buffer_pool=buffer_pool),
            indexes=indexes,
        )

    def _require_table(self, table_name: str) -> TableRuntime:
        if table_name not in self.tables:
            self.tables[table_name] = self._runtime_for_table(table_name)
        return self.tables[table_name]

    def _sync_stats(self, table_name: str, row_count: int | None = None) -> None:
        runtime = self._require_table(table_name)
        runtime.buffer_pool.flush_all_pages()
        if row_count is None:
            row_count = self.catalog.get_table(table_name).stats.row_count
        self.catalog.update_stats(
            table_name,
            TableStats(row_count=row_count, page_count=runtime.page_manager.page_count),
        )

    def _update_row_count(self, table_name: str, delta: int) -> None:
        current = self.catalog.get_table(table_name).stats.row_count
        self._sync_stats(table_name, row_count=max(current + delta, 0))

    def _iter_indexes(
        self, runtime: TableRuntime
    ) -> list[tuple[IndexMetadata, PersistentBPlusTree]]:
        return [
            (runtime.metadata.indexes[index_name], index)
            for index_name, index in runtime.indexes.items()
        ]

    @staticmethod
    def _serialize_row(row: dict[str, Value]) -> bytes:
        return json.dumps(row, sort_keys=True, separators=(",", ":")).encode("utf-8")

    @staticmethod
    def _deserialize_row(payload: bytes) -> dict[str, Value]:
        return json.loads(payload.decode("utf-8"))
