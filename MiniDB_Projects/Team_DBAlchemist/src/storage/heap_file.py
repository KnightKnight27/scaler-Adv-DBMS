"""
HeapFile: manages a binary file of fixed-size pages on disk.
Each page is PAGE_SIZE bytes. Page 0 is always the first page.
"""
import os
import struct
from storage.page import PAGE_SIZE


class HeapFile:
    def __init__(self, path: str):
        self.path = path
        if not os.path.exists(path):
            open(path, 'wb').close()

    @property
    def num_pages(self) -> int:
        size = os.path.getsize(self.path)
        return size // PAGE_SIZE

    def read_page(self, page_id: int) -> bytes:
        with open(self.path, 'rb') as f:
            f.seek(page_id * PAGE_SIZE)
            data = f.read(PAGE_SIZE)
        if len(data) < PAGE_SIZE:
            # page not fully written yet — return zeroed page
            data = data + b'\x00' * (PAGE_SIZE - len(data))
        return data

    def write_page(self, page_id: int, data: bytes | bytearray):
        assert len(data) == PAGE_SIZE
        with open(self.path, 'r+b') as f:
            f.seek(page_id * PAGE_SIZE)
            f.write(bytes(data))

    def allocate_page(self) -> int:
        """Append a blank page, return its page_id."""
        page_id = self.num_pages
        with open(self.path, 'ab') as f:
            f.write(b'\x00' * PAGE_SIZE)
        return page_id
