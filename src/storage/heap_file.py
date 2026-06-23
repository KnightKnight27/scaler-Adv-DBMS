"""
Heap File — Table storage using a collection of slotted pages.

A HeapFile manages all the pages that belong to a single table.
Records can be inserted, retrieved, deleted, and scanned sequentially.

Record Identifier (RID):
  (page_id, slot_id) — uniquely identifies a record within a heap file.
"""

from typing import Optional
from .disk_manager import DiskManager
from .page import SlottedPage, serialize_record, deserialize_record


class RID:
    """Record Identifier — uniquely identifies a record as (page_id, slot_id)."""

    def __init__(self, page_id: int, slot_id: int):
        self.page_id = page_id
        self.slot_id = slot_id

    def __eq__(self, other):
        if not isinstance(other, RID):
            return False
        return self.page_id == other.page_id and self.slot_id == other.slot_id

    def __hash__(self):
        return hash((self.page_id, self.slot_id))

    def __repr__(self):
        return f"RID({self.page_id}, {self.slot_id})"

    def to_tuple(self):
        return (self.page_id, self.slot_id)


class HeapFile:
    """
    Manages a heap file — an unordered collection of pages storing table records.

    Each table has one heap file. Pages are allocated as needed.
    A free-page directory tracks which pages have available space.

    Attributes:
        table_name: Name of the table this heap file stores.
        filename: The database file on disk.
        column_types: List of column type strings for record serialization.
    """

    def __init__(self, table_name: str, disk_manager: DiskManager,
                 buffer_pool=None, column_types: list = None):
        """
        Initialize a HeapFile.

        Args:
            table_name: Name of the table.
            disk_manager: DiskManager for page I/O.
            buffer_pool: Optional BufferPool for caching pages.
            column_types: List of column type strings (e.g., ['INTEGER', 'VARCHAR']).
        """
        self.table_name = table_name
        self.filename = f"{table_name}.db"
        self.disk_manager = disk_manager
        self.buffer_pool = buffer_pool
        self.column_types = column_types or []
        self._page_directory: dict = {}  # page_id -> estimated_free_space

        # Create the file if it doesn't exist
        disk_manager.create_file(self.filename)

        # Initialize page directory from existing pages
        self._build_page_directory()

    def _build_page_directory(self):
        """Scan existing pages to build the free-space directory."""
        page_count = self.disk_manager.get_page_count(self.filename)
        for pid in range(1, page_count):  # Skip header page 0
            page = self._read_page(pid)
            if page is not None:
                self._page_directory[pid] = page.free_space()

    def _read_page(self, page_id: int) -> Optional[SlottedPage]:
        """Read a page, using buffer pool if available."""
        try:
            if self.buffer_pool:
                return self.buffer_pool.get_page(self.filename, page_id)
            else:
                raw = self.disk_manager.read_page(self.filename, page_id)
                return SlottedPage.from_bytes(raw, self.disk_manager.page_size)
        except (ValueError, Exception):
            return None

    def _write_page(self, page: SlottedPage):
        """Write a page, using buffer pool if available."""
        if self.buffer_pool:
            self.buffer_pool.put_page(self.filename, page.page_id, page)
        else:
            self.disk_manager.write_page(self.filename, page.page_id, page.to_bytes())

    def _find_page_with_space(self, record_size: int) -> Optional[int]:
        """Find a page with enough free space for a record."""
        needed = record_size + 4  # Record + slot entry
        for pid, free in self._page_directory.items():
            if free >= needed:
                return pid
        return None

    def _allocate_new_page(self) -> SlottedPage:
        """Allocate a new data page."""
        page_id = self.disk_manager.allocate_page(self.filename)
        page = SlottedPage(page_id=page_id, page_size=self.disk_manager.page_size)
        self._write_page(page)
        self._page_directory[page_id] = page.free_space()
        return page

    def insert_record(self, values: list) -> RID:
        """
        Insert a record into the heap file.

        Args:
            values: List of Python values matching column_types.

        Returns:
            RID of the inserted record.
        """
        record_data = serialize_record(values, self.column_types)

        # Find a page with space
        page_id = self._find_page_with_space(len(record_data))

        if page_id is not None:
            page = self._read_page(page_id)
        else:
            page = self._allocate_new_page()
            page_id = page.page_id

        slot_id = page.insert_record(record_data)
        if slot_id is None:
            # Shouldn't happen if free space tracking is accurate, but handle it
            page = self._allocate_new_page()
            page_id = page.page_id
            slot_id = page.insert_record(record_data)

        self._write_page(page)
        self._page_directory[page_id] = page.free_space()

        return RID(page_id, slot_id)

    def get_record(self, rid: RID) -> Optional[list]:
        """
        Retrieve a record by its RID.

        Args:
            rid: Record identifier.

        Returns:
            List of Python values, or None if not found.
        """
        page = self._read_page(rid.page_id)
        if page is None:
            return None

        raw = page.get_record(rid.slot_id)
        if raw is None:
            return None

        return deserialize_record(raw, self.column_types)

    def get_raw_record(self, rid: RID) -> Optional[bytes]:
        """Retrieve raw record bytes by RID."""
        page = self._read_page(rid.page_id)
        if page is None:
            return None
        return page.get_record(rid.slot_id)

    def delete_record(self, rid: RID) -> bool:
        """
        Delete a record by its RID.

        Args:
            rid: Record identifier.

        Returns:
            True if deleted, False if not found.
        """
        page = self._read_page(rid.page_id)
        if page is None:
            return False

        result = page.delete_record(rid.slot_id)
        if result:
            self._write_page(page)
            self._page_directory[rid.page_id] = page.free_space()
        return result

    def update_record(self, rid: RID, values: list) -> bool:
        """
        Update a record in place.

        Args:
            rid: Record identifier.
            values: New values.

        Returns:
            True if updated, False if failed.
        """
        page = self._read_page(rid.page_id)
        if page is None:
            return False

        new_data = serialize_record(values, self.column_types)
        result = page.update_record(rid.slot_id, new_data)
        if result:
            self._write_page(page)
            self._page_directory[rid.page_id] = page.free_space()
        return result

    def scan(self):
        """
        Sequential scan — iterate over all live records.

        Yields:
            (RID, values_list) tuples for each record.
        """
        page_count = self.disk_manager.get_page_count(self.filename)
        for pid in range(1, page_count):  # Skip header page
            page = self._read_page(pid)
            if page is None:
                continue
            for slot_id, raw_record in page.get_all_records():
                try:
                    values = deserialize_record(raw_record, self.column_types)
                    yield RID(pid, slot_id), values
                except Exception:
                    continue

    def scan_raw(self):
        """
        Sequential scan yielding raw bytes.

        Yields:
            (RID, bytes) tuples for each record.
        """
        page_count = self.disk_manager.get_page_count(self.filename)
        for pid in range(1, page_count):
            page = self._read_page(pid)
            if page is None:
                continue
            for slot_id, raw_record in page.get_all_records():
                yield RID(pid, slot_id), raw_record

    def record_count(self) -> int:
        """Count all live records (full scan)."""
        count = 0
        page_count = self.disk_manager.get_page_count(self.filename)
        for pid in range(1, page_count):
            page = self._read_page(pid)
            if page is None:
                continue
            count += len(page.get_all_records())
        return count

    def page_count(self) -> int:
        """Return number of data pages (excluding header)."""
        return max(0, self.disk_manager.get_page_count(self.filename) - 1)

    def __repr__(self):
        return f"HeapFile(table={self.table_name}, pages={self.page_count()})"
