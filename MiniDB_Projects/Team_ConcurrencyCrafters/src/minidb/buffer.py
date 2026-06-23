from __future__ import annotations

from collections import OrderedDict

from .pages import Page, PageManager


class BufferPoolManager:
    def __init__(self, page_manager: PageManager, pool_size: int = 8):
        self.page_manager = page_manager
        self.pool_size = max(pool_size, 1)
        self.pages: dict[int, Page] = {}
        self.pin_count: dict[int, int] = {}
        self.dirty: dict[int, bool] = {}
        self.lru: OrderedDict[int, None] = OrderedDict()
        self.debug_logs: list[str] = []

    def _touch(self, page_id: int) -> None:
        self.lru.pop(page_id, None)
        self.lru[page_id] = None

    def _find_evictable(self) -> int | None:
        for page_id in list(self.lru.keys()):
            if self.pin_count.get(page_id, 0) == 0:
                return page_id
        return None

    def fetch_page(self, page_id: int) -> Page:
        if page_id in self.pages:
            self.pin_count[page_id] = self.pin_count.get(page_id, 0) + 1
            self._touch(page_id)
            self.debug_logs.append(f"buffer hit page={page_id}")
            return self.pages[page_id]

        if len(self.pages) >= self.pool_size:
            victim_id = self._find_evictable()
            if victim_id is None:
                raise RuntimeError("No evictable pages available in buffer pool.")
            victim = self.pages.pop(victim_id)
            if self.dirty.get(victim_id, False):
                self.page_manager.write_page(victim)
                self.debug_logs.append(f"buffer flush page={victim_id}")
            self.pin_count.pop(victim_id, None)
            self.dirty.pop(victim_id, None)
            self.lru.pop(victim_id, None)
            self.debug_logs.append(f"buffer eviction page={victim_id}")

        page = self.page_manager.read_page(page_id)
        self.pages[page_id] = page
        self.pin_count[page_id] = self.pin_count.get(page_id, 0) + 1
        self.dirty[page_id] = False
        self._touch(page_id)
        self.debug_logs.append(f"buffer miss page={page_id}")
        return page

    def new_page(self) -> Page:
        page_id = self.page_manager.allocate_page()
        page = self.fetch_page(page_id)
        self.debug_logs.append(f"buffer new_page page={page_id}")
        return page

    def unpin_page(self, page_id: int, is_dirty: bool) -> None:
        if page_id not in self.pages:
            return
        current = self.pin_count.get(page_id, 0)
        if current > 0:
            self.pin_count[page_id] = current - 1
        if is_dirty:
            self.dirty[page_id] = True
        self._touch(page_id)

    def flush_page(self, page_id: int) -> None:
        if page_id not in self.pages:
            return
        self.page_manager.write_page(self.pages[page_id])
        self.dirty[page_id] = False
        self.debug_logs.append(f"buffer flush page={page_id}")

    def flush_all_pages(self) -> None:
        for page_id in list(self.pages.keys()):
            self.flush_page(page_id)

