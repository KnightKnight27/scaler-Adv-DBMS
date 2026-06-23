"""
Page layout (4096 bytes):
  [page_id: 4B][num_slots: 2B][free_end: 2B] = 8B header
  [slot0: offset(2B)+length(2B)] ... slots grow forward
  ... free space ...
  [record_N] ... [record_1] [record_0]  records grow backward
"""
import struct

PAGE_SIZE = 4096
HEADER_SIZE = 8   # page_id(4) + num_slots(2) + free_end(2)
SLOT_SIZE = 4     # offset(2) + length(2)
TOMBSTONE = 0xFFFF  # marks deleted slot


class Page:
    def __init__(self, page_id: int, data: bytes = None):
        self.page_id = page_id
        self.dirty = False
        if data is not None:
            self.data = bytearray(data)
        else:
            self.data = bytearray(PAGE_SIZE)
            struct.pack_into('>IHH', self.data, 0, page_id, 0, PAGE_SIZE)

    # ── header accessors ─────────────────────────────────────────────────────

    @property
    def num_slots(self) -> int:
        return struct.unpack_from('>H', self.data, 4)[0]

    @property
    def free_end(self) -> int:
        return struct.unpack_from('>H', self.data, 6)[0]

    def _set_header(self, num_slots: int, free_end: int):
        struct.pack_into('>HH', self.data, 4, num_slots, free_end)

    # ── slot accessors ────────────────────────────────────────────────────────

    def _slot_offset(self, slot_id: int) -> int:
        return HEADER_SIZE + slot_id * SLOT_SIZE

    def _read_slot(self, slot_id: int):
        off = self._slot_offset(slot_id)
        return struct.unpack_from('>HH', self.data, off)

    def _write_slot(self, slot_id: int, rec_offset: int, rec_length: int):
        off = self._slot_offset(slot_id)
        struct.pack_into('>HH', self.data, off, rec_offset, rec_length)

    # ── free space ────────────────────────────────────────────────────────────

    def free_space(self) -> int:
        slot_area_end = HEADER_SIZE + self.num_slots * SLOT_SIZE
        return self.free_end - slot_area_end

    def has_space(self, rec_len: int) -> bool:
        # need room for new slot + record
        return self.free_space() >= SLOT_SIZE + rec_len

    # ── record operations ────────────────────────────────────────────────────

    def insert_record(self, record: bytes) -> int | None:
        rec_len = len(record)
        if not self.has_space(rec_len):
            return None

        new_free_end = self.free_end - rec_len
        self.data[new_free_end: new_free_end + rec_len] = record

        slot_id = self.num_slots
        self._write_slot(slot_id, new_free_end, rec_len)
        self._set_header(slot_id + 1, new_free_end)
        self.dirty = True
        return slot_id

    def get_record(self, slot_id: int) -> bytes | None:
        if slot_id >= self.num_slots:
            return None
        offset, length = self._read_slot(slot_id)
        if length == TOMBSTONE:
            return None  # deleted
        return bytes(self.data[offset: offset + length])

    def delete_record(self, slot_id: int) -> bool:
        if slot_id >= self.num_slots:
            return False
        offset, length = self._read_slot(slot_id)
        if length == TOMBSTONE:
            return False
        # mark tombstone — space not reclaimed (no compaction for simplicity)
        self._write_slot(slot_id, offset, TOMBSTONE)
        self.dirty = True
        return True

    def update_record(self, slot_id: int, record: bytes) -> bool:
        """In-place update only if new record fits in same space."""
        if slot_id >= self.num_slots:
            return False
        offset, length = self._read_slot(slot_id)
        if length == TOMBSTONE:
            return False
        if len(record) > length:
            return False  # doesn't fit — caller must delete+insert
        self.data[offset: offset + len(record)] = record
        self._write_slot(slot_id, offset, len(record))
        self.dirty = True
        return True

    def all_records(self):
        """Yield (slot_id, record_bytes) for all live records."""
        for slot_id in range(self.num_slots):
            rec = self.get_record(slot_id)
            if rec is not None:
                yield slot_id, rec
