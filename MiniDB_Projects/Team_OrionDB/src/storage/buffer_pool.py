from collections import OrderedDict
from src.storage.page import Page

class BufferPoolManager:
    def __init__(self, disk_manager, pool_size=10, wal_manager=None):
        self.disk_manager = disk_manager
        self.pool_size = pool_size
        self.wal_manager = wal_manager
        
        self.pool = {}        # page_id -> Page
        self.pin_count = {}   # page_id -> int
        self.dirty = {}       # page_id -> bool
        
        self.lru_keys = OrderedDict()

    def fetch_page(self, page_id):
        if page_id in self.pool:
            self.pin_count[page_id] += 1
            if page_id in self.lru_keys:
                del self.lru_keys[page_id]
            return self.pool[page_id]

        if len(self.pool) >= self.pool_size:
            if not self._evict_page():
                raise RuntimeError("Buffer pool is full! All pages are pinned.")

        # Read page from disk
        page_data = self.disk_manager.read_page(page_id)
        page = Page(page_id, page_data)
        
        self.pool[page_id] = page
        self.pin_count[page_id] = 1
        self.dirty[page_id] = False
        return page

    def unpin_page(self, page_id, is_dirty):
        if page_id not in self.pool:
            return
        
        if is_dirty:
            self.dirty[page_id] = True
            
        self.pin_count[page_id] -= 1
        assert self.pin_count[page_id] >= 0, f"Pin count for page {page_id} cannot be negative (current: {self.pin_count[page_id]})"
        
        if self.pin_count[page_id] == 0:
            if page_id in self.lru_keys:
                del self.lru_keys[page_id]
            self.lru_keys[page_id] = True

    def new_page(self):
        page_id = self.disk_manager.allocate_page()
        page = self.fetch_page(page_id)
        
        # Initialize header metadata for a newly allocated page
        page.set_page_id(page_id)
        page.set_lsn(0)
        page.set_num_slots(0)
        page.set_free_space_offset(Page.PAGE_SIZE)
        page.set_next_page_id(0xFFFFFFFF)
        
        self.dirty[page_id] = True
        return page

    def flush_page(self, page_id):
        if page_id not in self.pool:
            return
        
        if self.dirty[page_id]:
            page = self.pool[page_id]
            
            if self.wal_manager:
                self.wal_manager.flush_to_lsn(page.get_lsn())
                
            self.disk_manager.write_page(page_id, page.data)
            self.dirty[page_id] = False

    def flush_all(self):
        for page_id in list(self.pool.keys()):
            self.flush_page(page_id)

    def _evict_page(self):
        if not self.lru_keys:
            return False
        
        evict_page_id, _ = self.lru_keys.popitem(last=False)
        self.flush_page(evict_page_id)
        
        del self.pool[evict_page_id]
        del self.pin_count[evict_page_id]
        del self.dirty[evict_page_id]
        return True
