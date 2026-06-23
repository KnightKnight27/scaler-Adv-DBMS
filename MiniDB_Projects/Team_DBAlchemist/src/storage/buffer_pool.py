"""
Buffer pool with Clock Sweep replacement policy.

Why not LRU?
  - OS page cache already uses LRU on file blocks — a second LRU layer adds no value.
  - Sequential scans flood LRU: scanning 10k rows evicts all hot pages.

Clock Sweep (used by PostgreSQL):
  - Each frame has a usage_count (0..MAX_USAGE).
  - On access: increment usage_count (capped at MAX_USAGE).
  - On eviction needed: walk clock hand around the pool.
      - Pinned page   → skip.
      - usage_count>0 → decrement, advance hand (give page another chance).
      - usage_count=0 → evict (page had no recent accesses).
  - Scan pages are accessed once → count=1 → evicted in one clock pass.
  - Hot pages (B+ tree root, frequently joined table) accumulate high count
    → survive many sweeps, stay in pool.

This prevents sequential flooding while keeping hot data resident.
"""
from storage.page import Page
from storage.heap_file import HeapFile

MAX_USAGE = 5  # max usage count per frame — higher = hotter page survives longer


class _Frame:
    __slots__ = ('page', 'pin_count', 'usage_count')

    def __init__(self, page: Page):
        self.page = page
        self.pin_count = 0
        self.usage_count = 1  # newly loaded pages start at 1


class BufferPool:
    def __init__(self, heap_file: HeapFile, capacity: int = 128):
        self.heap = heap_file
        self.capacity = capacity
        self._frames: dict[int, _Frame] = {}   # page_id → _Frame
        self._clock_hand: list[int] = []        # ordered list of page_ids for clock
        self._hand_pos: int = 0                 # current clock hand position

    # ── public API ────────────────────────────────────────────────────────────

    def fetch_page(self, page_id: int) -> Page:
        if page_id in self._frames:
            frame = self._frames[page_id]
            frame.pin_count += 1
            frame.usage_count = min(frame.usage_count + 1, MAX_USAGE)
            return frame.page

        self._evict_if_needed()

        data = self.heap.read_page(page_id)
        page = Page(page_id, data)
        frame = _Frame(page)
        frame.pin_count = 1
        self._frames[page_id] = frame
        self._clock_hand.append(page_id)
        return page

    def new_page(self) -> Page:
        """Allocate a fresh page on disk, load into pool."""
        page_id = self.heap.allocate_page()
        self._evict_if_needed()
        page = Page(page_id)
        frame = _Frame(page)
        frame.pin_count = 1
        self._frames[page_id] = frame
        self._clock_hand.append(page_id)
        return page

    def unpin_page(self, page_id: int, dirty: bool = False):
        if page_id in self._frames:
            frame = self._frames[page_id]
            if dirty:
                frame.page.dirty = True
            frame.pin_count = max(0, frame.pin_count - 1)

    def flush_page(self, page_id: int):
        if page_id in self._frames:
            page = self._frames[page_id].page
            if page.dirty:
                self.heap.write_page(page_id, page.data)
                page.dirty = False

    def flush_all(self):
        for page_id in list(self._frames):
            self.flush_page(page_id)

    # ── clock sweep eviction ──────────────────────────────────────────────────

    def _evict_if_needed(self):
        if len(self._frames) < self.capacity:
            return

        # walk clock hand until we find a victim
        attempts = 0
        max_attempts = len(self._clock_hand) * (MAX_USAGE + 1)

        while attempts < max_attempts:
            if not self._clock_hand:
                return  # nothing to evict

            # wrap hand
            if self._hand_pos >= len(self._clock_hand):
                self._hand_pos = 0

            page_id = self._clock_hand[self._hand_pos]
            frame = self._frames.get(page_id)

            if frame is None:
                # stale entry — remove
                self._clock_hand.pop(self._hand_pos)
                continue

            if frame.pin_count > 0:
                # pinned — skip, advance
                self._hand_pos += 1
                attempts += 1
                continue

            if frame.usage_count > 0:
                # give this page another chance
                frame.usage_count -= 1
                self._hand_pos += 1
                attempts += 1
                continue

            # usage_count == 0 and unpinned → evict
            if frame.page.dirty:
                self.heap.write_page(page_id, frame.page.data)

            del self._frames[page_id]
            self._clock_hand.pop(self._hand_pos)
            # hand_pos stays (next page slides into this slot)
            return

        # all pinned or all hot — grow beyond capacity
