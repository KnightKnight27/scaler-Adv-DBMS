"""page.py — a fixed-size slotted page.

Layout of one PAGE_SIZE page:

    byte 0                                                      PAGE_SIZE
    +--------+----------------+...............+------------------+
    | header | slot directory |  free space   |   record data    |
    +--------+----------------+...............+------------------+
             grows forward ->                 <- grows backward

Header (4 bytes):
    [0:2] num_slots   (uint16)  number of slot entries
    [2:4] free_end    (uint16)  offset where record data begins (low end of data)

Slot directory: `num_slots` entries of SLOT_SIZE bytes each, each = (offset:uint16,
length:uint16). A slot whose offset == TOMBSTONE is a deleted record; its slot
number is never reused, so RIDs (page_id, slot) stay valid forever.

Delete policy: TOMBSTONE only (no in-page compaction) — the deleted bytes are
reclaimed only when the page is fully rewritten. This mirrors PostgreSQL, which
defers space reclamation to VACUUM, and keeps RIDs perfectly stable.
"""

from __future__ import annotations

import struct

from .constants import PAGE_SIZE, SLOT_SIZE, TOMBSTONE

_HEADER_FMT = "<HH"          # num_slots, free_end
HEADER_SIZE = struct.calcsize(_HEADER_FMT)  # = 4
_SLOT_FMT = "<HH"            # offset, length


class Page:
    """A slotted page backed by a fixed PAGE_SIZE bytearray."""

    def __init__(self, page_id: int, data: bytes | bytearray | None = None) -> None:
        self.page_id = page_id
        if data is None:
            self.data = bytearray(PAGE_SIZE)
            self._write_header(num_slots=0, free_end=PAGE_SIZE)
        else:
            if len(data) != PAGE_SIZE:
                raise ValueError(f"page data must be {PAGE_SIZE} bytes, got {len(data)}")
            self.data = bytearray(data)

    # --- header ------------------------------------------------------------

    def _read_header(self) -> tuple[int, int]:
        return struct.unpack_from(_HEADER_FMT, self.data, 0)

    def _write_header(self, num_slots: int, free_end: int) -> None:
        struct.pack_into(_HEADER_FMT, self.data, 0, num_slots, free_end)

    @property
    def num_slots(self) -> int:
        return self._read_header()[0]

    @property
    def free_end(self) -> int:
        return self._read_header()[1]

    @property
    def free_space(self) -> int:
        """Bytes available between the slot directory and the record data."""
        num_slots, free_end = self._read_header()
        slot_dir_end = HEADER_SIZE + num_slots * SLOT_SIZE
        return free_end - slot_dir_end

    # --- slots -------------------------------------------------------------

    def _slot_offset(self, slot: int) -> int:
        return HEADER_SIZE + slot * SLOT_SIZE

    def _read_slot(self, slot: int) -> tuple[int, int]:
        return struct.unpack_from(_SLOT_FMT, self.data, self._slot_offset(slot))

    def _write_slot(self, slot: int, offset: int, length: int) -> None:
        struct.pack_into(_SLOT_FMT, self.data, self._slot_offset(slot), offset, length)

    # --- public record API -------------------------------------------------

    def insert(self, record: bytes) -> int | None:
        """Insert a record, return its slot number, or None if it doesn't fit.

        Inserting needs room for the record bytes AND one new slot entry.
        """
        num_slots, free_end = self._read_header()
        need = len(record) + SLOT_SIZE
        if need > self.free_space:
            return None
        new_offset = free_end - len(record)
        self.data[new_offset:free_end] = record
        slot = num_slots
        self._write_slot(slot, new_offset, len(record))
        self._write_header(num_slots + 1, new_offset)
        return slot

    def get(self, slot: int) -> bytes | None:
        """Return the record bytes at `slot`, or None if deleted/out of range."""
        if slot < 0 or slot >= self.num_slots:
            return None
        offset, length = self._read_slot(slot)
        if offset == TOMBSTONE:
            return None
        return bytes(self.data[offset : offset + length])

    def delete(self, slot: int) -> bool:
        """Tombstone the record at `slot`. Returns False if already gone/invalid."""
        if slot < 0 or slot >= self.num_slots:
            return False
        offset, _ = self._read_slot(slot)
        if offset == TOMBSTONE:
            return False
        self._write_slot(slot, TOMBSTONE, TOMBSTONE)
        return True

    def is_deleted(self, slot: int) -> bool:
        if slot < 0 or slot >= self.num_slots:
            return True
        return self._read_slot(slot)[0] == TOMBSTONE

    def live_slots(self) -> list[int]:
        """Slot numbers of records that are still present (not tombstoned)."""
        return [s for s in range(self.num_slots) if not self.is_deleted(s)]

    def items(self):
        """Yield (slot, record_bytes) for every live record."""
        for s in self.live_slots():
            yield s, self.get(s)

    # --- serialization -----------------------------------------------------

    def to_bytes(self) -> bytes:
        """The full PAGE_SIZE image, ready to write to disk."""
        return bytes(self.data)
