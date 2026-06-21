from typing import Dict, List, Tuple
from src.storage.page import Page

class BufferPool:
    def __init__(self, capacity: int = 10, wal_manager=None):
        self.capacity = capacity
        self.wal_manager = wal_manager
        # Mapping from (table_name, page_id) -> Dict containing page, pin_count, is_dirty, heap_file
        self.pool = {}
        # LRU order: list of (table_name, page_id) keys
        self.lru_order = []

    def fetch_page(self, page_id: int, heap_file) -> Page:
        key = (heap_file.table_name, page_id)
        
        if key in self.pool:
            entry = self.pool[key]
            entry["pin_count"] += 1
            # Move to end of LRU list (most recently used)
            self.lru_order.remove(key)
            self.lru_order.append(key)
            return entry["page"]

        # If not in pool, check if capacity is reached
        if len(self.pool) >= self.capacity:
            # Find an eviction candidate (pin_count == 0) using LRU order
            evict_key = None
            for k in self.lru_order:
                if self.pool[k]["pin_count"] == 0:
                    evict_key = k
                    break
            
            if evict_key is None:
                raise RuntimeError("Buffer pool is full and all pages are pinned (cannot evict)")

            # Evict candidate
            self.evict_page(evict_key)

        # Read page from disk
        page_data = heap_file.read_page_from_disk(page_id)
        page = Page(page_id)
        page.deserialize(page_data)

        self.pool[key] = {
            "page": page,
            "pin_count": 1,
            "is_dirty": False,
            "heap_file": heap_file
        }
        self.lru_order.append(key)
        return page

    def unpin_page(self, page_id: int, heap_file, is_dirty: bool = False):
        key = (heap_file.table_name, page_id)
        if key in self.pool:
            entry = self.pool[key]
            entry["pin_count"] = max(0, entry["pin_count"] - 1)
            if is_dirty:
                entry["is_dirty"] = True
            
            # Update LRU order: move to end
            if key in self.lru_order:
                self.lru_order.remove(key)
            self.lru_order.append(key)

    def mark_dirty(self, page_id: int, heap_file):
        key = (heap_file.table_name, page_id)
        if key in self.pool:
            self.pool[key]["is_dirty"] = True

    def evict_page(self, key: Tuple[str, int]):
        entry = self.pool[key]
        if entry["is_dirty"]:
            self.flush_entry(key, entry)
        
        del self.pool[key]
        self.lru_order.remove(key)

    def flush_entry(self, key: Tuple[str, int], entry: dict):
        # Write-Ahead Logging rule: flush WAL to disk before writing the dirty page
        if self.wal_manager is not None:
            self.wal_manager.flush()
        
        table_name, page_id = key
        entry["heap_file"].write_page_to_disk(page_id, entry["page"].serialize())
        entry["is_dirty"] = False

    def flush_page(self, page_id: int, heap_file):
        key = (heap_file.table_name, page_id)
        if key in self.pool:
            entry = self.pool[key]
            if entry["is_dirty"]:
                self.flush_entry(key, entry)

    def flush_all(self):
        for key, entry in list(self.pool.items()):
            if entry["is_dirty"]:
                self.flush_entry(key, entry)
