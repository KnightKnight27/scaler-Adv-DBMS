"""
Page — Slotted Page format for MiniDB.

Implements a slotted page structure where records are stored from the end
of the page growing backwards, while a slot directory grows from the front.

Page Layout:
  ┌──────────────────────────────────────────────┐
  │ Header (record_count, free_space_ptr, flags) │  Fixed 12 bytes
  ├──────────────────────────────────────────────┤
  │ Slot Directory                               │  4 bytes per slot
  │   [offset: 2B, length: 2B] × N              │  (offset, length)
  ├──────────────────────────────────────────────┤
  │ Free Space                                   │
  ├──────────────────────────────────────────────┤
  │ Records (growing from bottom up)             │
  └──────────────────────────────────────────────┘

Record format:
  [null_bitmap: ceil(num_cols/8) bytes] [col1_data] [col2_data] ...
  Variable-length fields are stored as [length: 2B] [data].
  Fixed-length fields are stored inline.
"""

import struct
from typing import Optional


# Page header: [record_count: 2B] [free_space_offset: 2B] [free_space_end: 2B] [flags: 2B] [page_id: 4B]
PAGE_HEADER_FORMAT = '!HHHHI'
PAGE_HEADER_SIZE = struct.calcsize(PAGE_HEADER_FORMAT)  # 12 bytes

# Slot directory entry: [offset: 2B] [length: 2B]
SLOT_FORMAT = '!HH'
SLOT_SIZE = struct.calcsize(SLOT_FORMAT)  # 4 bytes

# Special values
SLOT_EMPTY = 0xFFFF  # Marks a deleted slot


class Page:
    """
    Represents a raw page of bytes.
    """

    def __init__(self, page_id: int = 0, page_size: int = 4096, data: bytes = None):
        self.page_id = page_id
        self.page_size = page_size
        if data is not None:
            self.data = bytearray(data)
        else:
            self.data = bytearray(page_size)

    def to_bytes(self) -> bytes:
        return bytes(self.data)

    @staticmethod
    def from_bytes(data: bytes, page_size: int = 4096) -> 'Page':
        page_id = struct.unpack_from('!I', data, 8)[0] if len(data) >= 12 else 0
        return Page(page_id=page_id, page_size=page_size, data=data)


class SlottedPage:
    """
    A page using the slotted-page format for storing variable-length records.

    The slot directory grows forward from the header, and records are placed
    from the end of the page growing backward. This allows efficient use of
    space for variable-length records.

    Attributes:
        page_id: Unique identifier for this page.
        page_size: Total size of the page in bytes.
    """

    def __init__(self, page_id: int = 0, page_size: int = 4096, data: bytes = None):
        self.page_id = page_id
        self.page_size = page_size

        if data is not None:
            self.data = bytearray(data)
            self._read_header()
        else:
            self.data = bytearray(page_size)
            self.record_count = 0
            self.free_space_offset = PAGE_HEADER_SIZE  # Start of free space (after header)
            self.free_space_end = page_size             # End of free space (before records)
            self.flags = 0
            self._write_header()

    def _read_header(self):
        """Read page header from data."""
        vals = struct.unpack_from(PAGE_HEADER_FORMAT, self.data, 0)
        self.record_count = vals[0]
        self.free_space_offset = vals[1]
        self.free_space_end = vals[2]
        self.flags = vals[3]
        self.page_id = vals[4]

    def _write_header(self):
        """Write page header to data."""
        struct.pack_into(PAGE_HEADER_FORMAT, self.data, 0,
                         self.record_count, self.free_space_offset,
                         self.free_space_end, self.flags, self.page_id)

    def _slot_offset(self, slot_id: int) -> int:
        """Calculate the byte offset of a slot directory entry."""
        return PAGE_HEADER_SIZE + slot_id * SLOT_SIZE

    def _get_slot(self, slot_id: int) -> tuple:
        """Read a slot directory entry. Returns (offset, length)."""
        off = self._slot_offset(slot_id)
        return struct.unpack_from(SLOT_FORMAT, self.data, off)

    def _set_slot(self, slot_id: int, offset: int, length: int):
        """Write a slot directory entry."""
        off = self._slot_offset(slot_id)
        struct.pack_into(SLOT_FORMAT, self.data, off, offset, length)

    def free_space(self) -> int:
        """Return the amount of free space available for new records."""
        return self.free_space_end - self.free_space_offset

    def can_fit(self, record_size: int) -> bool:
        """Check if a record of given size can fit in this page."""
        needed = record_size + SLOT_SIZE  # Record data + slot entry
        return self.free_space() >= needed

    def insert_record(self, record: bytes) -> Optional[int]:
        """
        Insert a record into the page.

        Args:
            record: The record data as bytes.

        Returns:
            The slot_id of the inserted record, or None if no space.
        """
        record_len = len(record)

        # Check for a deleted slot we can reuse
        reuse_slot = None
        for i in range(self.record_count):
            off, length = self._get_slot(i)
            if off == SLOT_EMPTY:
                reuse_slot = i
                break

        if reuse_slot is not None:
            # Reuse deleted slot — still need space for the record data
            if self.free_space_end - self.free_space_offset < record_len:
                return None
            # Place record at the end of free space
            record_offset = self.free_space_end - record_len
            self.data[record_offset:record_offset + record_len] = record
            self._set_slot(reuse_slot, record_offset, record_len)
            self.free_space_end = record_offset
            self._write_header()
            return reuse_slot
        else:
            # Need space for both slot and record
            if not self.can_fit(record_len):
                return None
            # Allocate new slot
            slot_id = self.record_count
            self.record_count += 1
            self.free_space_offset = PAGE_HEADER_SIZE + self.record_count * SLOT_SIZE

            # Place record at the end of free space
            record_offset = self.free_space_end - record_len
            self.data[record_offset:record_offset + record_len] = record
            self._set_slot(slot_id, record_offset, record_len)
            self.free_space_end = record_offset
            self._write_header()
            return slot_id

    def get_record(self, slot_id: int) -> Optional[bytes]:
        """
        Retrieve a record by slot_id.

        Args:
            slot_id: The slot index.

        Returns:
            Record data as bytes, or None if slot is empty/invalid.
        """
        if slot_id < 0 or slot_id >= self.record_count:
            return None
        offset, length = self._get_slot(slot_id)
        if offset == SLOT_EMPTY:
            return None
        return bytes(self.data[offset:offset + length])

    def delete_record(self, slot_id: int) -> bool:
        """
        Delete a record by marking its slot as empty.

        Args:
            slot_id: The slot to delete.

        Returns:
            True if deleted successfully, False if slot was already empty.
        """
        if slot_id < 0 or slot_id >= self.record_count:
            return False
        offset, length = self._get_slot(slot_id)
        if offset == SLOT_EMPTY:
            return False
        self._set_slot(slot_id, SLOT_EMPTY, 0)
        self._write_header()
        return True

    def update_record(self, slot_id: int, new_record: bytes) -> bool:
        """
        Update a record in-place if it fits, otherwise return False.

        Args:
            slot_id: Slot to update.
            new_record: New record data.

        Returns:
            True if updated, False if not enough space.
        """
        if slot_id < 0 or slot_id >= self.record_count:
            return False
        offset, length = self._get_slot(slot_id)
        if offset == SLOT_EMPTY:
            return False

        if len(new_record) <= length:
            # Fits in existing space — write in place
            self.data[offset:offset + len(new_record)] = new_record
            if len(new_record) < length:
                # Zero out remaining old data
                self.data[offset + len(new_record):offset + length] = b'\x00' * (length - len(new_record))
            self._set_slot(slot_id, offset, len(new_record))
            return True
        else:
            # Doesn't fit — delete and re-insert
            self.delete_record(slot_id)
            # Check if we have space for the new record (no new slot needed since we reuse)
            if self.free_space_end - self.free_space_offset < len(new_record):
                return False
            record_offset = self.free_space_end - len(new_record)
            self.data[record_offset:record_offset + len(new_record)] = new_record
            self._set_slot(slot_id, record_offset, len(new_record))
            self.free_space_end = record_offset
            self._write_header()
            return True

    def get_all_records(self) -> list:
        """
        Get all live records as list of (slot_id, record_bytes).

        Returns:
            List of (slot_id, bytes) tuples for non-deleted records.
        """
        records = []
        for i in range(self.record_count):
            offset, length = self._get_slot(i)
            if offset != SLOT_EMPTY:
                records.append((i, bytes(self.data[offset:offset + length])))
        return records

    def compact(self):
        """
        Compact the page by removing gaps between records.

        Moves all records to be contiguous at the end of the page,
        eliminating fragmentation from deleted records.
        """
        records = []
        for i in range(self.record_count):
            offset, length = self._get_slot(i)
            if offset != SLOT_EMPTY:
                records.append((i, bytes(self.data[offset:offset + length])))

        # Reset the data area
        self.free_space_end = self.page_size
        for slot_id, record_data in records:
            record_len = len(record_data)
            record_offset = self.free_space_end - record_len
            self.data[record_offset:record_offset + record_len] = record_data
            self._set_slot(slot_id, record_offset, record_len)
            self.free_space_end = record_offset

        self._write_header()

    def to_bytes(self) -> bytes:
        """Serialize the page to bytes."""
        return bytes(self.data)

    @staticmethod
    def from_bytes(data: bytes, page_size: int = 4096) -> 'SlottedPage':
        """Deserialize a page from bytes."""
        page = SlottedPage.__new__(SlottedPage)
        page.page_size = page_size
        page.data = bytearray(data)
        page._read_header()
        return page

    def __repr__(self):
        return (f"SlottedPage(id={self.page_id}, records={self.record_count}, "
                f"free={self.free_space()})")


# ─── Record Serialization Utilities ───────────────────────────────────────────

# Supported column types
COLUMN_TYPES = {
    'INTEGER': 'i',     # 4 bytes, signed
    'FLOAT': 'f',       # 4 bytes
    'BOOLEAN': '?',     # 1 byte
    'VARCHAR': 's',     # variable length
    'TEXT': 's',        # variable length (alias)
}


def serialize_record(values: list, column_types: list) -> bytes:
    """
    Serialize a record (list of Python values) into bytes.

    Format: [null_bitmap] [field1] [field2] ...
    Variable-length fields: [2B length] [data bytes]
    Fixed-length fields: packed directly

    Args:
        values: List of Python values (None for NULL).
        column_types: List of type strings ('INTEGER', 'VARCHAR', etc.).

    Returns:
        Serialized record as bytes.
    """
    num_cols = len(column_types)
    null_bitmap_size = (num_cols + 7) // 8
    null_bitmap = bytearray(null_bitmap_size)

    parts = [None]  # Placeholder for null bitmap

    for i, (val, col_type) in enumerate(zip(values, column_types)):
        if val is None:
            null_bitmap[i // 8] |= (1 << (i % 8))
            # No data written for NULL
            continue

        col_type_upper = col_type.upper()
        if col_type_upper == 'INTEGER':
            parts.append(struct.pack('!i', int(val)))
        elif col_type_upper == 'FLOAT':
            parts.append(struct.pack('!f', float(val)))
        elif col_type_upper == 'BOOLEAN':
            parts.append(struct.pack('!?', bool(val)))
        elif col_type_upper in ('VARCHAR', 'TEXT'):
            encoded = str(val).encode('utf-8')
            parts.append(struct.pack('!H', len(encoded)) + encoded)
        else:
            raise ValueError(f"Unknown column type: {col_type}")

    parts[0] = bytes(null_bitmap)
    return b''.join(parts)


def deserialize_record(data: bytes, column_types: list) -> list:
    """
    Deserialize bytes back into a list of Python values.

    Args:
        data: Raw record bytes.
        column_types: List of type strings.

    Returns:
        List of Python values (None for NULL fields).
    """
    num_cols = len(column_types)
    null_bitmap_size = (num_cols + 7) // 8
    null_bitmap = data[:null_bitmap_size]
    offset = null_bitmap_size

    values = []
    for i, col_type in enumerate(column_types):
        # Check null bitmap
        is_null = (null_bitmap[i // 8] >> (i % 8)) & 1
        if is_null:
            values.append(None)
            continue

        col_type_upper = col_type.upper()
        if col_type_upper == 'INTEGER':
            val = struct.unpack_from('!i', data, offset)[0]
            offset += 4
            values.append(val)
        elif col_type_upper == 'FLOAT':
            val = struct.unpack_from('!f', data, offset)[0]
            offset += 4
            values.append(val)
        elif col_type_upper == 'BOOLEAN':
            val = struct.unpack_from('!?', data, offset)[0]
            offset += 1
            values.append(val)
        elif col_type_upper in ('VARCHAR', 'TEXT'):
            str_len = struct.unpack_from('!H', data, offset)[0]
            offset += 2
            val = data[offset:offset + str_len].decode('utf-8')
            offset += str_len
            values.append(val)
        else:
            raise ValueError(f"Unknown column type: {col_type}")

    return values
