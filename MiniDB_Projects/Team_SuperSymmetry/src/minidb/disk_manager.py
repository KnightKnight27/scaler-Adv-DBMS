"""
Disk manager: page-granular reads/writes to on-disk files.

Each logical file (a table heap, an index, etc.) lives in its own OS file
inside the database directory. A PageId is (file_key, page_num). The disk
manager knows nothing about record contents -- it only moves PAGE_SIZE
blocks between memory and disk.
"""
from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Dict

from .page import PAGE_SIZE, Page


@dataclass(frozen=True)
class PageId:
    file_key: str
    page_num: int


class DiskManager:
    def __init__(self, directory: str):
        self.directory = directory
        os.makedirs(directory, exist_ok=True)
        self._fds: Dict[str, int] = {}

    def _path(self, file_key: str) -> str:
        return os.path.join(self.directory, file_key + ".dat")

    def _fd(self, file_key: str) -> int:
        if file_key not in self._fds:
            path = self._path(file_key)
            fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o644)
            self._fds[file_key] = fd
        return self._fds[file_key]

    def num_pages(self, file_key: str) -> int:
        fd = self._fd(file_key)
        size = os.fstat(fd).st_size
        return size // PAGE_SIZE

    def allocate_page(self, file_key: str) -> PageId:
        """Append a fresh zeroed page to a file and return its PageId."""
        n = self.num_pages(file_key)
        fd = self._fd(file_key)
        os.pwrite(fd, bytes(PAGE_SIZE), n * PAGE_SIZE)
        return PageId(file_key, n)

    def read_page(self, pid: PageId) -> Page:
        fd = self._fd(pid.file_key)
        raw = os.pread(fd, PAGE_SIZE, pid.page_num * PAGE_SIZE)
        if len(raw) < PAGE_SIZE:
            raw = raw + bytes(PAGE_SIZE - len(raw))
        return Page(bytearray(raw))

    def write_page(self, pid: PageId, page: Page):
        fd = self._fd(pid.file_key)
        os.pwrite(fd, bytes(page.data), pid.page_num * PAGE_SIZE)

    def fsync(self, file_key: str):
        os.fsync(self._fd(file_key))

    def fsync_all(self):
        for fd in self._fds.values():
            os.fsync(fd)

    def close(self):
        for fd in self._fds.values():
            os.close(fd)
        self._fds.clear()
