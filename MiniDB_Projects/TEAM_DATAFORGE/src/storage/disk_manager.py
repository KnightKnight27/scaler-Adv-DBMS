"""
Disk Manager — Low-level page I/O for MiniDB.

Manages fixed-size page reads/writes to database files on disk.
Each table gets its own file. Pages are addressed by page_id (0-indexed).

Design:
  - Fixed page size (default 4096 bytes)
  - File header (page 0) stores metadata: page count, free-page list head
  - Thread-safe I/O operations
"""

import os
import struct
import threading


# Default page size: 4KB
PAGE_SIZE = 4096

# File header format (stored in page 0):
#   [magic: 4B] [page_count: 4B] [free_list_head: 4B] [reserved: rest]
HEADER_MAGIC = b'MNDB'
HEADER_FORMAT = '!4sII'  # magic, page_count, free_list_head
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
FREE_LIST_NONE = 0xFFFFFFFF


class DiskManager:
    """
    Manages reading and writing fixed-size pages to/from database files.

    Each database file is a sequence of fixed-size pages. Page 0 is the file
    header. User pages start from page 1.

    Attributes:
        page_size: Size of each page in bytes.
        base_dir: Directory where database files are stored.
    """

    def __init__(self, base_dir: str, page_size: int = PAGE_SIZE):
        """
        Initialize the DiskManager.

        Args:
            base_dir: Directory to store all database files.
            page_size: Size of each page in bytes.
        """
        self.page_size = page_size
        self.base_dir = base_dir
        self._files: dict[str, any] = {}  # filename -> file handle
        self._locks: dict[str, threading.Lock] = {}
        self._global_lock = threading.Lock()

        os.makedirs(base_dir, exist_ok=True)

    def _get_filepath(self, filename: str) -> str:
        """Get the full file path for a database file."""
        return os.path.join(self.base_dir, filename)

    def _get_file(self, filename: str):
        """Get or open a file handle for the given filename."""
        if filename not in self._files:
            with self._global_lock:
                if filename not in self._files:
                    filepath = self._get_filepath(filename)
                    if os.path.exists(filepath):
                        fh = open(filepath, 'r+b')
                    else:
                        fh = open(filepath, 'w+b')
                        # Write initial header
                        self._write_header(fh, page_count=1, free_list_head=FREE_LIST_NONE)
                    self._files[filename] = fh
                    self._locks[filename] = threading.Lock()
        return self._files[filename]

    def _get_lock(self, filename: str) -> threading.Lock:
        """Get the lock for a specific file."""
        self._get_file(filename)  # Ensure file and lock exist
        return self._locks[filename]

    def _write_header(self, fh, page_count: int, free_list_head: int):
        """Write the file header to page 0."""
        header_data = struct.pack(HEADER_FORMAT, HEADER_MAGIC, page_count, free_list_head)
        # Pad to full page
        page_data = header_data + b'\x00' * (self.page_size - len(header_data))
        fh.seek(0)
        fh.write(page_data)
        fh.flush()

    def _read_header(self, fh) -> tuple:
        """Read the file header from page 0. Returns (page_count, free_list_head)."""
        fh.seek(0)
        raw = fh.read(HEADER_SIZE)
        if len(raw) < HEADER_SIZE:
            return (1, FREE_LIST_NONE)
        magic, page_count, free_list_head = struct.unpack(HEADER_FORMAT, raw)
        if magic != HEADER_MAGIC:
            raise ValueError(f"Invalid database file (bad magic: {magic})")
        return (page_count, free_list_head)

    def create_file(self, filename: str):
        """
        Create a new database file with an initialized header.

        Args:
            filename: Name of the database file (e.g., 'employees.db').
        """
        filepath = self._get_filepath(filename)
        if os.path.exists(filepath):
            return  # Already exists
        self._get_file(filename)  # This creates and initializes the file

    def allocate_page(self, filename: str) -> int:
        """
        Allocate a new page in the file. Returns the page_id.

        The page is zero-filled initially.

        Args:
            filename: Database file to allocate in.

        Returns:
            The page_id of the newly allocated page.
        """
        lock = self._get_lock(filename)
        fh = self._get_file(filename)

        with lock:
            page_count, free_list_head = self._read_header(fh)

            if free_list_head != FREE_LIST_NONE:
                # Reuse a page from the free list
                page_id = free_list_head
                # Read the free page to get the next free page pointer
                fh.seek(page_id * self.page_size)
                raw = fh.read(4)
                next_free = struct.unpack('!I', raw)[0]
                # Zero out the reused page
                fh.seek(page_id * self.page_size)
                fh.write(b'\x00' * self.page_size)
                fh.flush()
                # Update header
                self._write_header(fh, page_count, next_free)
                return page_id
            else:
                # Append a new page
                new_page_id = page_count
                fh.seek(new_page_id * self.page_size)
                fh.write(b'\x00' * self.page_size)
                fh.flush()
                # Update header
                self._write_header(fh, page_count + 1, free_list_head)
                return new_page_id

    def deallocate_page(self, filename: str, page_id: int):
        """
        Deallocate a page, adding it to the free list.

        Args:
            filename: Database file name.
            page_id: Page to deallocate (must be > 0).
        """
        if page_id == 0:
            raise ValueError("Cannot deallocate header page (page 0)")

        lock = self._get_lock(filename)
        fh = self._get_file(filename)

        with lock:
            page_count, free_list_head = self._read_header(fh)
            # Write the current free list head into the deallocated page
            fh.seek(page_id * self.page_size)
            fh.write(struct.pack('!I', free_list_head))
            fh.write(b'\x00' * (self.page_size - 4))
            fh.flush()
            # Update header to point to this page as new free list head
            self._write_header(fh, page_count, page_id)

    def read_page(self, filename: str, page_id: int) -> bytes:
        """
        Read a page from disk.

        Args:
            filename: Database file name.
            page_id: Page to read.

        Returns:
            Raw page data as bytes (exactly page_size bytes).

        Raises:
            ValueError: If page_id is out of range.
        """
        lock = self._get_lock(filename)
        fh = self._get_file(filename)

        with lock:
            page_count, _ = self._read_header(fh)
            if page_id < 0 or page_id >= page_count:
                raise ValueError(f"Page {page_id} out of range [0, {page_count})")

            fh.seek(page_id * self.page_size)
            data = fh.read(self.page_size)

            if len(data) < self.page_size:
                data += b'\x00' * (self.page_size - len(data))

            return data

    def write_page(self, filename: str, page_id: int, data: bytes):
        """
        Write a page to disk.

        Args:
            filename: Database file name.
            page_id: Page to write.
            data: Raw page data (must be exactly page_size bytes).

        Raises:
            ValueError: If data size doesn't match page_size or page_id is invalid.
        """
        if len(data) != self.page_size:
            raise ValueError(f"Page data must be {self.page_size} bytes, got {len(data)}")

        lock = self._get_lock(filename)
        fh = self._get_file(filename)

        with lock:
            page_count, _ = self._read_header(fh)
            if page_id < 0 or page_id >= page_count:
                raise ValueError(f"Page {page_id} out of range [0, {page_count})")

            fh.seek(page_id * self.page_size)
            fh.write(data)
            fh.flush()

    def get_page_count(self, filename: str) -> int:
        """Get the total number of pages (including header) in a file."""
        lock = self._get_lock(filename)
        fh = self._get_file(filename)
        with lock:
            page_count, _ = self._read_header(fh)
            return page_count

    def close(self):
        """Close all open file handles."""
        with self._global_lock:
            for fh in self._files.values():
                fh.close()
            self._files.clear()
            self._locks.clear()

    def close_file(self, filename: str):
        """Close a specific file handle."""
        with self._global_lock:
            if filename in self._files:
                self._files[filename].close()
                del self._files[filename]
                del self._locks[filename]

    def delete_file(self, filename: str):
        """Delete a database file from disk."""
        self.close_file(filename)
        filepath = self._get_filepath(filename)
        if os.path.exists(filepath):
            os.remove(filepath)

    def sync(self, filename: str):
        """Force flush file to disk (fsync)."""
        if filename in self._files:
            fh = self._files[filename]
            fh.flush()
            os.fsync(fh.fileno())
