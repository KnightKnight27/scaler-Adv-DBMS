from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

PAGE_SIZE = 4096
PAGE_HEADER_SIZE = 16
SLOT_SIZE = 8
SLOT_STRUCT = struct.Struct("<HHB3x")


@dataclass
class Page:
    page_id: int
    data: bytearray

    @classmethod
    def empty(cls, page_id: int) -> "Page":
        page = cls(page_id=page_id, data=bytearray(PAGE_SIZE))
        page.num_slots = 0
        page.free_end = PAGE_SIZE
        return page

    @classmethod
    def from_bytes(cls, page_id: int, raw: bytes) -> "Page":
        if len(raw) != PAGE_SIZE:
            raise ValueError(f"Expected {PAGE_SIZE} bytes, got {len(raw)}.")
        return cls(page_id=page_id, data=bytearray(raw))

    def to_bytes(self) -> bytes:
        return bytes(self.data)

    @property
    def num_slots(self) -> int:
        return struct.unpack_from("<H", self.data, 0)[0]

    @num_slots.setter
    def num_slots(self, value: int) -> None:
        struct.pack_into("<H", self.data, 0, value)

    @property
    def free_end(self) -> int:
        return struct.unpack_from("<H", self.data, 2)[0]

    @free_end.setter
    def free_end(self, value: int) -> None:
        struct.pack_into("<H", self.data, 2, value)

    def _slot_offset(self, slot_id: int) -> int:
        return PAGE_HEADER_SIZE + slot_id * SLOT_SIZE

    def _read_slot(self, slot_id: int) -> tuple[int, int, int]:
        if slot_id < 0 or slot_id >= self.num_slots:
            raise IndexError(f"Slot {slot_id} is out of range.")
        return SLOT_STRUCT.unpack_from(self.data, self._slot_offset(slot_id))

    def _write_slot(self, slot_id: int, offset: int, length: int, flags: int) -> None:
        SLOT_STRUCT.pack_into(self.data, self._slot_offset(slot_id), offset, length, flags)

    def free_space(self) -> int:
        slot_directory_end = PAGE_HEADER_SIZE + self.num_slots * SLOT_SIZE
        return self.free_end - slot_directory_end

    def can_fit(self, payload_length: int) -> bool:
        return self.free_space() >= payload_length + SLOT_SIZE

    def insert(self, payload: bytes) -> int:
        if not self.can_fit(len(payload)):
            raise ValueError("Page does not have enough free space for payload.")
        new_free_end = self.free_end - len(payload)
        self.data[new_free_end : self.free_end] = payload
        slot_id = self.num_slots
        self._write_slot(slot_id, new_free_end, len(payload), 0)
        self.num_slots += 1
        self.free_end = new_free_end
        return slot_id

    def read(self, slot_id: int, include_deleted: bool = False) -> bytes | None:
        offset, length, flags = self._read_slot(slot_id)
        if flags == 1 and not include_deleted:
            return None
        return bytes(self.data[offset : offset + length])

    def delete(self, slot_id: int) -> None:
        offset, length, _ = self._read_slot(slot_id)
        self._write_slot(slot_id, offset, length, 1)

    def restore(self, slot_id: int, payload: bytes) -> None:
        offset, length, _ = self._read_slot(slot_id)
        if len(payload) > length:
            raise ValueError("Cannot restore a payload larger than the original slot.")
        self.data[offset : offset + len(payload)] = payload
        if len(payload) < length:
            self.data[offset + len(payload) : offset + length] = b" " * (length - len(payload))
        self._write_slot(slot_id, offset, length, 0)

    def iter_records(self) -> list[tuple[int, bytes]]:
        rows: list[tuple[int, bytes]] = []
        for slot_id in range(self.num_slots):
            payload = self.read(slot_id)
            if payload is not None:
                rows.append((slot_id, payload))
        return rows

    def iter_record_ids(self) -> list[int]:
        active_slots: list[int] = []
        for slot_id in range(self.num_slots):
            payload = self.read(slot_id)
            if payload is not None:
                active_slots.append(slot_id)
        return active_slots


class PageManager:
    def __init__(self, file_path: str | Path):
        self.file_path = Path(file_path)
        self.file_path.parent.mkdir(parents=True, exist_ok=True)
        if not self.file_path.exists():
            self.file_path.write_bytes(b"")

    @property
    def page_count(self) -> int:
        return self.file_path.stat().st_size // PAGE_SIZE

    def allocate_page(self) -> int:
        page_id = self.page_count
        page = Page.empty(page_id)
        with self.file_path.open("ab") as handle:
            handle.write(page.to_bytes())
        return page_id

    def read_page(self, page_id: int) -> Page:
        with self.file_path.open("rb") as handle:
            handle.seek(page_id * PAGE_SIZE)
            raw = handle.read(PAGE_SIZE)
        if len(raw) != PAGE_SIZE:
            raise ValueError(f"Page {page_id} does not exist in {self.file_path}.")
        return Page.from_bytes(page_id, raw)

    def write_page(self, page: Page) -> None:
        with self.file_path.open("r+b") as handle:
            handle.seek(page.page_id * PAGE_SIZE)
            handle.write(page.to_bytes())

