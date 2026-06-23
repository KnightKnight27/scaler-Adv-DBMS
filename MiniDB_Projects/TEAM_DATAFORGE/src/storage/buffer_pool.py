"""
Buffer Pool — In-memory page cache with LRU replacement for MiniDB.

The buffer pool sits between the execution engine and the disk manager,
caching frequently accessed pages in memory to reduce disk I/O.

Design:
  - Fixed number of frames (configurable)
  - LRU (Least Recently Used) replacement policy
  - Pin/unpin with reference counting
  - Dirty page tracking and flush-on-eviction
  - Thread-safe operations
"""

import threading
from collections import OrderedDict
from typing import Optional

from .disk_manager import DiskManager
from .page import SlottedPage


class BufferFrame:
    """
    A single frame in the buffer pool holding one page.

    Attributes:
        page: The SlottedPage stored in this frame.
        filename: The file this page belongs to.
        page_id: The page ID.
        pin_count: Number of active references.
        is_dirty: Whether the page has been modified since last flush.
    """

    def __init__(self):
        self.page: Optional[SlottedPage] = None
        self.filename: Optional[str] = None
        self.page_id: int = -1
        self.pin_count: int = 0
        self.is_dirty: bool = False

    def reset(self):
        self.page = None
        self.filename = None
        self.page_id = -1
        self.pin_count = 0
        self.is_dirty = False

    @property
    def key(self):
        return (self.filename, self.page_id)


class BufferPool:
    """
    Buffer Pool Manager — caches database pages in memory using LRU eviction.

    Usage:
        pool = BufferPool(disk_manager, pool_size=100)
        page = pool.get_page('table.db', page_id=5)
        # ... modify page ...
        pool.put_page('table.db', 5, page)  # mark dirty
        pool.unpin('table.db', 5)
        pool.flush_all()

    Attributes:
        pool_size: Maximum number of pages in the pool.
        hit_count: Number of cache hits.
        miss_count: Number of cache misses.
    """

    def __init__(self, disk_manager: DiskManager, pool_size: int = 100):
        """
        Initialize the buffer pool.

        Args:
            disk_manager: The disk manager for page I/O.
            pool_size: Maximum number of frames in the pool.
        """
        self.disk_manager = disk_manager
        self.pool_size = pool_size
        self._page_table: OrderedDict[tuple, BufferFrame] = OrderedDict()
        self._lock = threading.Lock()

        # Statistics
        self.hit_count = 0
        self.miss_count = 0

    def get_page(self, filename: str, page_id: int) -> SlottedPage:
        """
        Fetch a page from the buffer pool. Loads from disk on cache miss.

        The page is automatically pinned (pin_count incremented).

        Args:
            filename: Database file name.
            page_id: Page ID to fetch.

        Returns:
            The SlottedPage.
        """
        key = (filename, page_id)

        with self._lock:
            if key in self._page_table:
                # Cache hit
                self.hit_count += 1
                frame = self._page_table[key]
                frame.pin_count += 1
                # Move to end (most recently used)
                self._page_table.move_to_end(key)
                return frame.page
            else:
                # Cache miss
                self.miss_count += 1

                # Evict if necessary
                if len(self._page_table) >= self.pool_size:
                    self._evict_one()

                # Load from disk
                raw_data = self.disk_manager.read_page(filename, page_id)
                page = SlottedPage.from_bytes(raw_data, self.disk_manager.page_size)
                page.page_id = page_id

                # Create frame
                frame = BufferFrame()
                frame.page = page
                frame.filename = filename
                frame.page_id = page_id
                frame.pin_count = 1
                frame.is_dirty = False

                self._page_table[key] = frame
                return page

    def put_page(self, filename: str, page_id: int, page: SlottedPage):
        """
        Put a modified page into the buffer pool (marks as dirty).

        If the page is already in the pool, updates it. Otherwise adds it.

        Args:
            filename: Database file name.
            page_id: Page ID.
            page: The modified SlottedPage.
        """
        key = (filename, page_id)

        with self._lock:
            if key in self._page_table:
                frame = self._page_table[key]
                frame.page = page
                frame.is_dirty = True
                self._page_table.move_to_end(key)
            else:
                if len(self._page_table) >= self.pool_size:
                    self._evict_one()

                frame = BufferFrame()
                frame.page = page
                frame.filename = filename
                frame.page_id = page_id
                frame.pin_count = 0
                frame.is_dirty = True

                self._page_table[key] = frame

    def unpin(self, filename: str, page_id: int, is_dirty: bool = False):
        """
        Unpin a page (decrement reference count).

        Args:
            filename: Database file name.
            page_id: Page ID.
            is_dirty: Whether the page was modified.
        """
        key = (filename, page_id)
        with self._lock:
            if key in self._page_table:
                frame = self._page_table[key]
                if frame.pin_count > 0:
                    frame.pin_count -= 1
                if is_dirty:
                    frame.is_dirty = True

    def _evict_one(self):
        """
        Evict the least recently used unpinned page.

        Must be called with self._lock held.

        Raises:
            RuntimeError: If no unpinned pages are available for eviction.
        """
        # Find the LRU unpinned page
        for key in list(self._page_table.keys()):
            frame = self._page_table[key]
            if frame.pin_count == 0:
                # Flush if dirty
                if frame.is_dirty:
                    self._flush_frame(frame)
                del self._page_table[key]
                return

        raise RuntimeError("Buffer pool is full: all pages are pinned")

    def _flush_frame(self, frame: BufferFrame):
        """Write a dirty frame back to disk."""
        self.disk_manager.write_page(frame.filename, frame.page_id, frame.page.to_bytes())
        frame.is_dirty = False

    def flush_page(self, filename: str, page_id: int):
        """Flush a specific page to disk."""
        key = (filename, page_id)
        with self._lock:
            if key in self._page_table:
                frame = self._page_table[key]
                if frame.is_dirty:
                    self._flush_frame(frame)

    def flush_all(self):
        """Flush all dirty pages to disk."""
        with self._lock:
            for frame in self._page_table.values():
                if frame.is_dirty:
                    self._flush_frame(frame)

    def invalidate(self, filename: str, page_id: int):
        """Remove a page from the buffer pool without flushing."""
        key = (filename, page_id)
        with self._lock:
            if key in self._page_table:
                del self._page_table[key]

    def get_stats(self) -> dict:
        """Get buffer pool statistics."""
        total = self.hit_count + self.miss_count
        hit_rate = (self.hit_count / total * 100) if total > 0 else 0.0
        return {
            'pool_size': self.pool_size,
            'pages_in_pool': len(self._page_table),
            'hit_count': self.hit_count,
            'miss_count': self.miss_count,
            'hit_rate': f"{hit_rate:.1f}%",
            'dirty_pages': sum(1 for f in self._page_table.values() if f.is_dirty),
            'pinned_pages': sum(1 for f in self._page_table.values() if f.pin_count > 0),
        }

    def reset_stats(self):
        """Reset hit/miss counters."""
        self.hit_count = 0
        self.miss_count = 0

    def __repr__(self):
        stats = self.get_stats()
        return (f"BufferPool(size={self.pool_size}, "
                f"used={stats['pages_in_pool']}, "
                f"hit_rate={stats['hit_rate']})")
