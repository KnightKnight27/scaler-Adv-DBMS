"""disk_manager.py — page-granular I/O over a single database file.

The disk manager treats the database as a flat array of fixed-size pages stored
back-to-back in one file:

    file = [ page 0 ][ page 1 ][ page 2 ] ...   (each PAGE_SIZE bytes)

It is intentionally minimal — it knows nothing about tables, tuples, or slots.
Its whole job is: allocate a fresh page, read page N, write page N, and (for
durability) flush to physical disk. Higher layers build meaning on top.

Use path=":memory:" for an in-memory database (handy for tests); any other path
is a real file that persists across process restarts (needed for recovery demos).
"""

from __future__ import annotations

import io
import os

from .constants import PAGE_SIZE


class DiskManager:
    def __init__(self, path: str = ":memory:") -> None:
        self.path = path
        self.in_memory = path == ":memory:"
        # I/O counters (used by demos/benchmarks to show real disk activity).
        self.reads = 0
        self.writes = 0
        if self.in_memory:
            self._f: io.BufferedRandom | io.BytesIO = io.BytesIO()
        else:
            # "r+b" requires existing file; create it first if missing.
            if not os.path.exists(path):
                open(path, "wb").close()
            self._f = open(path, "r+b")
        self._f.seek(0, os.SEEK_END)
        self._size = self._f.tell()

    @property
    def num_pages(self) -> int:
        return self._size // PAGE_SIZE

    def _check_id(self, page_id: int) -> None:
        if page_id < 0 or page_id >= self.num_pages:
            raise ValueError(
                f"page {page_id} out of range (have {self.num_pages} pages)"
            )

    def allocate_page(self) -> int:
        """Append a fresh zeroed page and return its page id."""
        page_id = self.num_pages
        self._f.seek(self._size)
        self._f.write(bytes(PAGE_SIZE))
        self._size += PAGE_SIZE
        return page_id

    def read_page(self, page_id: int) -> bytes:
        """Return the PAGE_SIZE bytes of page `page_id`."""
        self._check_id(page_id)
        self._f.seek(page_id * PAGE_SIZE)
        data = self._f.read(PAGE_SIZE)
        if len(data) != PAGE_SIZE:  # defensive: short read => corruption/truncation
            raise IOError(f"short read on page {page_id}: got {len(data)} bytes")
        self.reads += 1
        return data

    def write_page(self, page_id: int, data: bytes) -> None:
        """Overwrite page `page_id` with `data` (must be exactly PAGE_SIZE)."""
        if len(data) != PAGE_SIZE:
            raise ValueError(f"write must be {PAGE_SIZE} bytes, got {len(data)}")
        self._check_id(page_id)
        self._f.seek(page_id * PAGE_SIZE)
        self._f.write(data)
        self.writes += 1

    def flush(self) -> None:
        """Force buffered writes to physical disk (durability point for WAL)."""
        self._f.flush()
        if not self.in_memory:
            os.fsync(self._f.fileno())

    def close(self) -> None:
        self.flush()
        self._f.close()
