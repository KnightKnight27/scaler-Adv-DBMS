"""
Slotted-page layout for variable-length records.

Page layout (PAGE_SIZE bytes):

    +--------------------------------------------------------------+
    | header (16 bytes)                                            |
    |   page_lsn   : uint64   (log sequence number, for WAL)      |
    |   num_slots  : uint16                                        |
    |   free_end   : uint16   (offset where free space ends, i.e. |
    |                          lowest record byte written so far)  |
    |   reserved   : 4 bytes                                       |
    +--------------------------------------------------------------+
    | slot array (grows downward from header)                     |
    |   each slot: offset uint16, length uint16                   |
    |   length == 0 means the slot is a tombstone (deleted)       |
    +--------------------------------------------------------------+
    | ...... free space ......                                    |
    +--------------------------------------------------------------+
    | record data (grows upward from end of page)                 |
    +--------------------------------------------------------------+

A record is addressed by its slot number; (page_id, slot) forms the RID.
"""
from __future__ import annotations

import struct
from typing import List, Optional

PAGE_SIZE = 4096
HEADER_SIZE = 16
SLOT_SIZE = 4  # offset(2) + length(2)


class Page:
    def __init__(self, data: Optional[bytearray] = None):
        if data is None:
            self.data = bytearray(PAGE_SIZE)
            self._set_header(page_lsn=0, num_slots=0, free_end=PAGE_SIZE)
        else:
            assert len(data) == PAGE_SIZE
            self.data = bytearray(data)

    # ---- header ----------------------------------------------------------
    def _set_header(self, page_lsn: int, num_slots: int, free_end: int):
        struct.pack_into("<QHH4x", self.data, 0, page_lsn, num_slots, free_end)

    @property
    def page_lsn(self) -> int:
        return struct.unpack_from("<Q", self.data, 0)[0]

    @page_lsn.setter
    def page_lsn(self, lsn: int):
        struct.pack_into("<Q", self.data, 0, lsn)

    @property
    def num_slots(self) -> int:
        return struct.unpack_from("<H", self.data, 8)[0]

    @num_slots.setter
    def num_slots(self, n: int):
        struct.pack_into("<H", self.data, 8, n)

    @property
    def free_end(self) -> int:
        return struct.unpack_from("<H", self.data, 10)[0]

    @free_end.setter
    def free_end(self, v: int):
        struct.pack_into("<H", self.data, 10, v)

    # ---- slot helpers ----------------------------------------------------
    def _slot_pos(self, slot: int) -> int:
        return HEADER_SIZE + slot * SLOT_SIZE

    def _get_slot(self, slot: int):
        off, ln = struct.unpack_from("<HH", self.data, self._slot_pos(slot))
        return off, ln

    def _put_slot(self, slot: int, off: int, ln: int):
        struct.pack_into("<HH", self.data, self._slot_pos(slot), off, ln)

    def free_space(self) -> int:
        slot_area_end = HEADER_SIZE + self.num_slots * SLOT_SIZE
        return self.free_end - slot_area_end

    # ---- record operations ----------------------------------------------
    def can_fit(self, record: bytes) -> bool:
        # need room for the record plus possibly a new slot
        return self.free_space() >= len(record) + SLOT_SIZE

    def insert_record(self, record: bytes) -> Optional[int]:
        """Insert a record, returning its slot number, or None if no space."""
        # Try to reuse a tombstoned slot first.
        reuse = None
        for s in range(self.num_slots):
            _, ln = self._get_slot(s)
            if ln == 0:
                reuse = s
                break
        need = len(record) + (0 if reuse is not None else SLOT_SIZE)
        if self.free_space() < need:
            return None
        new_end = self.free_end - len(record)
        self.data[new_end : self.free_end] = record
        self.free_end = new_end
        if reuse is not None:
            self._put_slot(reuse, new_end, len(record))
            return reuse
        slot = self.num_slots
        self._put_slot(slot, new_end, len(record))
        self.num_slots = slot + 1
        return slot

    def get_record(self, slot: int) -> Optional[bytes]:
        if slot >= self.num_slots:
            return None
        off, ln = self._get_slot(slot)
        if ln == 0:
            return None
        return bytes(self.data[off : off + ln])

    def delete_record(self, slot: int) -> bool:
        if slot >= self.num_slots:
            return False
        off, ln = self._get_slot(slot)
        if ln == 0:
            return False
        # Mark tombstone. (We don't compact space here for simplicity;
        # compaction would be a vacuum operation.)
        self._put_slot(slot, 0, 0)
        return True

    def update_record(self, slot: int, record: bytes) -> bool:
        """In-place update if it fits in the old footprint, else delete+insert
        into the SAME slot when possible. Returns False if it cannot fit."""
        if slot >= self.num_slots:
            return False
        off, ln = self._get_slot(slot)
        if ln == 0:
            return False
        if len(record) <= ln:
            self.data[off : off + len(record)] = record
            self._put_slot(slot, off, len(record))
            return True
        # Needs to relocate. Free old, allocate new at free_end.
        if self.free_space() < len(record):
            return False
        new_end = self.free_end - len(record)
        self.data[new_end : self.free_end] = record
        self.free_end = new_end
        self._put_slot(slot, new_end, len(record))
        return True

    def iter_slots(self):
        for s in range(self.num_slots):
            rec = self.get_record(s)
            if rec is not None:
                yield s, rec
