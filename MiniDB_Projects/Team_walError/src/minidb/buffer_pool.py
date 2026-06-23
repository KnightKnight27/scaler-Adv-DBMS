"""buffer_pool.py — a fixed-size page cache with clock-sweep eviction.

The buffer pool sits between the engine and the disk manager. It keeps a bounded
number of pages resident in memory (frames) and decides which to evict when full.

Eviction = clock-sweep (PostgreSQL's algorithm, an approximation of LRU):
  * each frame has a reference bit, set whenever the page is accessed;
  * a rotating "clock hand" sweeps frames looking for a victim;
  * a frame that is pinned (in use) is skipped;
  * a frame whose ref bit is set gets a "second chance": clear the bit, move on;
  * the first unpinned frame with a clear ref bit is evicted (flushed if dirty).

Pinning: `fetch_page`/`new_page` return a *pinned* page. The caller MUST call
`unpin_page(page_id, dirty=...)` when done, declaring whether it modified the
page. Pinned pages are never evicted — this is what keeps an in-use page valid.
"""

from __future__ import annotations

from dataclasses import dataclass

from .constants import DEFAULT_POOL_FRAMES
from .disk_manager import DiskManager
from .page import Page


@dataclass
class _Frame:
    page: Page
    dirty: bool = False
    pin_count: int = 0
    ref_bit: bool = False


class BufferPoolFullError(RuntimeError):
    """Raised when every frame is pinned and no page can be evicted."""


class BufferPool:
    def __init__(self, disk: DiskManager, num_frames: int = DEFAULT_POOL_FRAMES) -> None:
        if num_frames < 1:
            raise ValueError("buffer pool needs at least 1 frame")
        self.disk = disk
        self.num_frames = num_frames
        self._frames: list[_Frame | None] = [None] * num_frames
        self._table: dict[int, int] = {}  # page_id -> frame index
        self._hand = 0
        self.hits = 0
        self.misses = 0

    # --- public API --------------------------------------------------------

    def new_page(self) -> Page:
        """Allocate a brand-new page on disk and return it pinned + dirty."""
        page_id = self.disk.allocate_page()
        page = Page(page_id)
        frame_idx = self._load_into_frame(page)
        f = self._frames[frame_idx]
        assert f is not None
        f.dirty = True  # a new page must eventually be written
        f.pin_count += 1
        f.ref_bit = True
        return page

    def fetch_page(self, page_id: int) -> Page:
        """Return page `page_id` (pinned), from cache (hit) or disk (miss)."""
        if page_id in self._table:
            self.hits += 1
            f = self._frames[self._table[page_id]]
            assert f is not None
            f.pin_count += 1
            f.ref_bit = True
            return f.page
        self.misses += 1
        page = Page(page_id, self.disk.read_page(page_id))
        frame_idx = self._load_into_frame(page)
        f = self._frames[frame_idx]
        assert f is not None
        f.pin_count += 1
        f.ref_bit = True
        return page

    def unpin_page(self, page_id: int, dirty: bool = False) -> None:
        """Release one pin on a page; OR in whether the caller dirtied it."""
        idx = self._table.get(page_id)
        if idx is None:
            raise KeyError(f"page {page_id} is not in the buffer pool")
        f = self._frames[idx]
        assert f is not None
        if f.pin_count <= 0:
            raise RuntimeError(f"page {page_id} unpinned more times than pinned")
        f.pin_count -= 1
        if dirty:
            f.dirty = True

    def flush_page(self, page_id: int) -> None:
        """Write a single page back to disk if dirty; clears its dirty flag."""
        idx = self._table.get(page_id)
        if idx is None:
            return
        f = self._frames[idx]
        assert f is not None
        if f.dirty:
            self.disk.write_page(page_id, f.page.to_bytes())
            f.dirty = False

    def flush_all(self) -> None:
        """Write every dirty resident page back to disk, then fsync."""
        for f in self._frames:
            if f is not None and f.dirty:
                self.disk.write_page(f.page.page_id, f.page.to_bytes())
                f.dirty = False
        self.disk.flush()

    @property
    def hit_ratio(self) -> float:
        total = self.hits + self.misses
        return self.hits / total if total else 0.0

    # --- internals ---------------------------------------------------------

    def _load_into_frame(self, page: Page) -> int:
        """Place `page` into a free or evicted frame; return the frame index."""
        # 1) use a free frame if one exists
        for i, f in enumerate(self._frames):
            if f is None:
                self._frames[i] = _Frame(page=page)
                self._table[page.page_id] = i
                return i
        # 2) otherwise evict a victim via clock-sweep
        victim = self._choose_victim()
        old = self._frames[victim]
        assert old is not None
        if old.dirty:
            self.disk.write_page(old.page.page_id, old.page.to_bytes())
        del self._table[old.page.page_id]
        self._frames[victim] = _Frame(page=page)
        self._table[page.page_id] = victim
        return victim

    def _choose_victim(self) -> int:
        """Clock-sweep: return the frame index to evict, or raise if all pinned."""
        # At most 2 full sweeps are ever needed (one to clear ref bits, one to evict).
        for _ in range(2 * self.num_frames):
            f = self._frames[self._hand]
            idx = self._hand
            self._hand = (self._hand + 1) % self.num_frames
            assert f is not None  # only called when pool is full
            if f.pin_count > 0:
                continue  # pinned: never evict
            if f.ref_bit:
                f.ref_bit = False  # second chance
                continue
            return idx
        raise BufferPoolFullError(
            "all frames are pinned; cannot evict (increase pool size or unpin pages)"
        )
