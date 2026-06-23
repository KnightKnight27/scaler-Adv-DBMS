"""
Heap file: an unordered collection of records spread across pages of a
single file, accessed through the buffer pool.

A record is addressed by a RID = (page_num, slot). The heap file supports
insert, get, update, delete and a full scan. Inserts go to the current
"insertion page"; when it is full a new page is allocated.

Each mutating call accepts an optional `lsn` used to stamp the page's LSN
for write-ahead logging and idempotent redo during recovery.
"""
from __future__ import annotations

from typing import Iterator, Optional, Tuple

from .buffer_pool import BufferPool
from .disk_manager import PageId
from .page import Page

RID = Tuple[int, int]  # (page_num, slot)


class HeapFile:
    def __init__(self, bufferpool: BufferPool, file_key: str):
        self.bp = bufferpool
        self.file_key = file_key
        self._insert_hint = max(0, self.bp.disk.num_pages(file_key) - 1)
        if self.bp.disk.num_pages(file_key) == 0:
            pid, _ = self.bp.new_page(file_key)
            self.bp.unpin_page(pid, dirty=True)
            self._insert_hint = 0

    def _pid(self, page_num: int) -> PageId:
        return PageId(self.file_key, page_num)

    # ---- mutators --------------------------------------------------------
    def insert(self, record: bytes, lsn: int = 0) -> RID:
        n = self.bp.disk.num_pages(self.file_key)
        # try the hint page, then any page, then allocate
        candidates = list(range(self._insert_hint, n)) + list(
            range(0, self._insert_hint)
        )
        for pno in candidates:
            pid = self._pid(pno)
            page = self.bp.fetch_page(pid)
            try:
                if page.can_fit(record):
                    slot = page.insert_record(record)
                    if slot is not None:
                        if lsn:
                            page.page_lsn = lsn
                        self.bp.unpin_page(pid, dirty=True)
                        self._insert_hint = pno
                        return (pno, slot)
            finally:
                if pid in self.bp.frames and self.bp.frames[pid].pin_count > 0:
                    self.bp.unpin_page(pid, dirty=False)
        # need a new page
        pid, page = self.bp.new_page(self.file_key)
        slot = page.insert_record(record)
        assert slot is not None
        if lsn:
            page.page_lsn = lsn
        self.bp.unpin_page(pid, dirty=True)
        self._insert_hint = pid.page_num
        return (pid.page_num, slot)

    def get(self, rid: RID) -> Optional[bytes]:
        pid = self._pid(rid[0])
        page = self.bp.fetch_page(pid)
        try:
            return page.get_record(rid[1])
        finally:
            self.bp.unpin_page(pid, dirty=False)

    def delete(self, rid: RID, lsn: int = 0) -> bool:
        pid = self._pid(rid[0])
        page = self.bp.fetch_page(pid)
        try:
            ok = page.delete_record(rid[1])
            if ok and lsn:
                page.page_lsn = lsn
            self.bp.unpin_page(pid, dirty=ok)
            return ok
        finally:
            if pid in self.bp.frames and self.bp.frames[pid].pin_count > 0:
                self.bp.unpin_page(pid, dirty=False)

    def update(self, rid: RID, record: bytes, lsn: int = 0) -> bool:
        pid = self._pid(rid[0])
        page = self.bp.fetch_page(pid)
        try:
            ok = page.update_record(rid[1], record)
            if ok and lsn:
                page.page_lsn = lsn
            self.bp.unpin_page(pid, dirty=ok)
            return ok
        finally:
            if pid in self.bp.frames and self.bp.frames[pid].pin_count > 0:
                self.bp.unpin_page(pid, dirty=False)

    # ---- redo helper (recovery) -----------------------------------------
    def redo_set(self, rid: RID, record: Optional[bytes], lsn: int):
        """Idempotently set a slot's contents during recovery redo. If the
        page already reflects this LSN, skip (already applied)."""
        pid = self._pid(rid[0])
        # ensure page exists
        while self.bp.disk.num_pages(self.file_key) <= rid[0]:
            npid, _ = self.bp.new_page(self.file_key)
            self.bp.unpin_page(npid, dirty=True)
        page = self.bp.fetch_page(pid)
        try:
            if page.page_lsn >= lsn:
                return
            if record is None:
                page.delete_record(rid[1])
            else:
                # ensure slot exists up to rid[1]
                while page.num_slots <= rid[1]:
                    page.insert_record(b"")
                    page._put_slot(page.num_slots - 1, 0, 0)
                if not page.update_record(rid[1], record):
                    # slot was tombstoned: write fresh at free space
                    new_end = page.free_end - len(record)
                    page.data[new_end : page.free_end] = record
                    page.free_end = new_end
                    page._put_slot(rid[1], new_end, len(record))
            page.page_lsn = lsn
            self.bp.unpin_page(pid, dirty=True)
        finally:
            if pid in self.bp.frames and self.bp.frames[pid].pin_count > 0:
                self.bp.unpin_page(pid, dirty=False)

    def set_lsn(self, rid: RID, lsn: int):
        """Stamp a page LSN after an insert whose record bytes are already
        written (used to honour the WAL rule once the log LSN is known)."""
        pid = self._pid(rid[0])
        page = self.bp.fetch_page(pid)
        if lsn > page.page_lsn:
            page.page_lsn = lsn
        self.bp.unpin_page(pid, dirty=True)

    # ---- scan ------------------------------------------------------------
    def scan(self) -> Iterator[Tuple[RID, bytes]]:
        n = self.bp.disk.num_pages(self.file_key)
        for pno in range(n):
            pid = self._pid(pno)
            page = self.bp.fetch_page(pid)
            try:
                rows = list(page.iter_slots())
            finally:
                self.bp.unpin_page(pid, dirty=False)
            for slot, rec in rows:
                yield (pno, slot), rec
