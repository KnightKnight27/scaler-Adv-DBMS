"""heap.py — a heap file: an unordered collection of records across pages.

A table's rows live in a sequence of slotted data pages owned by one HeapFile.
Each record is addressed by a RID (record id) = (page_id, slot). RIDs are stable
for the life of a record (slots are never renumbered), so indexes can store them.

Insert strategy: try the most-recently-used page first; if the record doesn't
fit, allocate a new page and append it. Because deletes are tombstone-only (the
page never compacts), reusable free space only collects in the tail page, so this
append-mostly policy is both simple and a good fit. Trade-off: small leftover
gaps in earlier pages are not reclaimed — Postgres solves this with a free-space
map + VACUUM, which MiniDB intentionally omits for clarity.

Ownership of the page-id list: the HeapFile holds it in memory; the catalog is
responsible for persisting it so the table can be reopened.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

from .buffer_pool import BufferPool
from .page import Page

_RID_FMT = "<qH"  # page_id (8-byte signed), slot (2-byte unsigned)
RID_SIZE = struct.calcsize(_RID_FMT)  # = 10


@dataclass(frozen=True)
class RID:
    """Physical record address: which page, which slot."""

    page_id: int
    slot: int

    def to_bytes(self) -> bytes:
        return struct.pack(_RID_FMT, self.page_id, self.slot)

    @classmethod
    def from_bytes(cls, data: bytes) -> "RID":
        page_id, slot = struct.unpack(_RID_FMT, data)
        return cls(page_id, slot)


class HeapFile:
    def __init__(self, buffer_pool: BufferPool, page_ids: list[int] | None = None) -> None:
        self.bp = buffer_pool
        # ordered list of data pages owned by this table (persisted by catalog)
        self.page_ids: list[int] = list(page_ids) if page_ids else []

    # --- mutations ---------------------------------------------------------

    def insert(self, record: bytes) -> RID:
        """Insert a record and return its RID."""
        # 1) fast path: try the tail page
        if self.page_ids:
            tail = self.page_ids[-1]
            page = self.bp.fetch_page(tail)
            slot = page.insert(record)
            if slot is not None:
                self.bp.unpin_page(tail, dirty=True)
                return RID(tail, slot)
            # didn't fit: unpin (not dirtied) and fall through to a new page
            self.bp.unpin_page(tail, dirty=False)
        # 2) allocate a fresh page and insert there
        page = self.bp.new_page()
        pid = page.page_id
        slot = page.insert(record)
        if slot is None:
            self.bp.unpin_page(pid, dirty=True)
            raise ValueError(
                f"record of {len(record)} bytes is too large for an empty page"
            )
        self.page_ids.append(pid)
        self.bp.unpin_page(pid, dirty=True)
        return RID(pid, slot)

    def delete(self, rid: RID) -> bool:
        """Tombstone the record at `rid`. Returns False if it wasn't present."""
        if rid.page_id not in self.page_ids:
            return False
        page = self.bp.fetch_page(rid.page_id)
        ok = page.delete(rid.slot)
        self.bp.unpin_page(rid.page_id, dirty=ok)
        return ok

    def update_in_place(self, rid: RID, record: bytes) -> bool:
        """Overwrite a record only if the new bytes are the same length.

        Returns False if lengths differ (caller should delete+insert instead) or
        the rid is gone. Same-length in-place update keeps the RID stable.
        """
        page = self.bp.fetch_page(rid.page_id)
        try:
            old = page.get(rid.slot)
            if old is None or len(old) != len(record):
                return False
            offset, length = page._read_slot(rid.slot)
            page.data[offset : offset + length] = record
            return True
        finally:
            self.bp.unpin_page(rid.page_id, dirty=True)

    # --- reads -------------------------------------------------------------

    def get(self, rid: RID) -> bytes | None:
        """Return the record bytes at `rid`, or None if missing/deleted."""
        if rid.page_id not in self.page_ids:
            return None
        page = self.bp.fetch_page(rid.page_id)
        rec = page.get(rid.slot)
        self.bp.unpin_page(rid.page_id, dirty=False)
        return rec

    def scan(self):
        """Yield (RID, record_bytes) for every live record, in page/slot order.

        This is the physical sequential scan that the executor's SeqScan operator
        pulls from.
        """
        for pid in self.page_ids:
            page = self.bp.fetch_page(pid)
            try:
                live = list(page.items())  # materialize before unpin
            finally:
                self.bp.unpin_page(pid, dirty=False)
            for slot, rec in live:
                yield RID(pid, slot), rec

    def __len__(self) -> int:
        """Number of live records (counts a full scan)."""
        return sum(1 for _ in self.scan())
