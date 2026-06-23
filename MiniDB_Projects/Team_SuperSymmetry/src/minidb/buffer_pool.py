"""
Buffer pool (page cache).

Holds a bounded number of frames in memory. Implements:
  * fetch/pin and unpin with reference counting
  * dirty-page tracking
  * LRU eviction of unpinned frames
  * the Write-Ahead-Logging rule: before a dirty data page is flushed to
    disk, the log must be durable up to that page's LSN. This is enforced
    via an optional `log_flush` callback wired to the WAL manager.

Statistics (hits/misses/evictions) are tracked so the buffer pool's effect
can be demonstrated and benchmarked.
"""
from __future__ import annotations

from collections import OrderedDict
from typing import Callable, Dict, Optional

from .disk_manager import DiskManager, PageId
from .page import Page


class Frame:
    __slots__ = ("page", "pin_count", "dirty")

    def __init__(self, page: Page):
        self.page = page
        self.pin_count = 0
        self.dirty = False


class BufferPool:
    def __init__(
        self,
        disk: DiskManager,
        capacity: int = 64,
        log_flush: Optional[Callable[[int], None]] = None,
    ):
        self.disk = disk
        self.capacity = capacity
        self.log_flush = log_flush  # called with a page LSN before flush
        self.frames: "OrderedDict[PageId, Frame]" = OrderedDict()
        self.hits = 0
        self.misses = 0
        self.evictions = 0

    # ---- core ------------------------------------------------------------
    def fetch_page(self, pid: PageId) -> Page:
        """Return the (pinned) Page for pid. Caller must unpin when done."""
        if pid in self.frames:
            self.hits += 1
            frame = self.frames[pid]
            frame.pin_count += 1
            self.frames.move_to_end(pid)  # mark most-recently-used
            return frame.page
        self.misses += 1
        if len(self.frames) >= self.capacity:
            self._evict_one()
        page = self.disk.read_page(pid)
        frame = Frame(page)
        frame.pin_count = 1
        self.frames[pid] = frame
        return page

    def new_page(self, file_key: str):
        """Allocate a fresh page on disk and return (PageId, Page). The
        returned Page is initialized (proper header), pinned, and dirty."""
        pid = self.disk.allocate_page(file_key)
        if len(self.frames) >= self.capacity:
            self._evict_one()
        page = Page()  # constructor initializes an empty slotted page
        frame = Frame(page)
        frame.pin_count = 1
        frame.dirty = True
        self.frames[pid] = frame
        return pid, page

    def unpin_page(self, pid: PageId, dirty: bool):
        frame = self.frames.get(pid)
        if frame is None:
            return
        if dirty:
            frame.dirty = True
        if frame.pin_count > 0:
            frame.pin_count -= 1

    def flush_page(self, pid: PageId):
        frame = self.frames.get(pid)
        if frame is None or not frame.dirty:
            return
        if self.log_flush is not None:
            self.log_flush(frame.page.page_lsn)  # WAL rule
        self.disk.write_page(pid, frame.page)
        frame.dirty = False

    def flush_all(self):
        for pid in list(self.frames.keys()):
            self.flush_page(pid)
        self.disk.fsync_all()

    # ---- eviction --------------------------------------------------------
    def _evict_one(self):
        for pid, frame in list(self.frames.items()):  # LRU order
            if frame.pin_count == 0:
                if frame.dirty:
                    self.flush_page(pid)
                del self.frames[pid]
                self.evictions += 1
                return
        raise RuntimeError("buffer pool full: all pages pinned")

    def stats(self) -> Dict[str, int]:
        total = self.hits + self.misses
        return {
            "hits": self.hits,
            "misses": self.misses,
            "evictions": self.evictions,
            "hit_rate_pct": round(100 * self.hits / total, 1) if total else 0.0,
        }
