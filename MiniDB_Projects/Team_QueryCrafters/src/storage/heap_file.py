import os
from typing import Iterator, Tuple
from src.storage.page import Page

class HeapFile:
    def __init__(self, table_name: str, data_dir: str = None, buffer_pool=None):
        if data_dir is None:
            self.file_path = table_name
            self.data_dir = os.path.dirname(table_name) or "."
            self.table_name = os.path.basename(table_name).split(".")[0]
        else:
            self.table_name = table_name
            self.data_dir = data_dir
            self.file_path = os.path.join(data_dir, f"{table_name}.db")
            
        os.makedirs(self.data_dir, exist_ok=True)
        # Touch file if it doesn't exist
        if not os.path.exists(self.file_path):
            with open(self.file_path, "wb") as f:
                pass
                
        if buffer_pool is None:
            from src.storage.buffer_pool import BufferPool
            self.buffer_pool = BufferPool(capacity=10)
        else:
            self.buffer_pool = buffer_pool

    def get_num_pages(self) -> int:
        file_size = os.path.getsize(self.file_path)
        return file_size // Page.PAGE_SIZE

    def read_page_from_disk(self, page_id: int) -> bytes:
        offset = page_id * Page.PAGE_SIZE
        with open(self.file_path, "rb") as f:
            f.seek(offset)
            data = f.read(Page.PAGE_SIZE)
            if len(data) < Page.PAGE_SIZE:
                # If page is empty or incomplete, return 4096 zero bytes
                return b"\x00" * Page.PAGE_SIZE
            return data

    def write_page_to_disk(self, page_id: int, page_data: bytes):
        if len(page_data) != Page.PAGE_SIZE:
            raise ValueError(f"Page data must be exactly {Page.PAGE_SIZE} bytes")
        offset = page_id * Page.PAGE_SIZE
        with open(self.file_path, "r+b" if os.path.exists(self.file_path) else "wb") as f:
            f.seek(offset)
            f.write(page_data)
            f.flush()

    def allocate_new_page(self) -> int:
        num_pages = self.get_num_pages()
        new_page_id = num_pages
        # Write an empty page to disk
        empty_page = Page(new_page_id)
        self.write_page_to_disk(new_page_id, empty_page.serialize())
        return new_page_id

    def insert_record(self, data) -> Tuple[int, int]:
        if self.buffer_pool is None:
            raise ValueError("Buffer pool is not set for this HeapFile")

        num_pages = self.get_num_pages()
        
        # Try inserting in the last page first
        if num_pages > 0:
            last_page_id = num_pages - 1
            page = self.buffer_pool.fetch_page(last_page_id, self)
            slot_id = page.add_record(data)
            if slot_id != -1:
                self.buffer_pool.unpin_page(last_page_id, self, is_dirty=True)
                return (last_page_id, slot_id)
            else:
                self.buffer_pool.unpin_page(last_page_id, self, is_dirty=False)

        # If last page was full or no pages exist, allocate a new page
        new_page_id = self.allocate_new_page()
        page = self.buffer_pool.fetch_page(new_page_id, self)
        slot_id = page.add_record(data)
        if slot_id == -1:
            self.buffer_pool.unpin_page(new_page_id, self, is_dirty=False)
            raise RuntimeError("Failed to insert record into a newly allocated page")
        self.buffer_pool.unpin_page(new_page_id, self, is_dirty=True)
        return (new_page_id, slot_id)

    def get_record(self, page_id: int, slot_id: int) -> bytes:
        if self.buffer_pool is None:
            raise ValueError("Buffer pool is not set for this HeapFile")
        
        page = self.buffer_pool.fetch_page(page_id, self)
        record = page.get_record(slot_id)
        self.buffer_pool.unpin_page(page_id, self, is_dirty=False)
        return record

    def delete_record(self, page_id: int, slot_id: int) -> bool:
        if self.buffer_pool is None:
            raise ValueError("Buffer pool is not set for this HeapFile")

        page = self.buffer_pool.fetch_page(page_id, self)
        success = page.delete_record(slot_id)
        self.buffer_pool.unpin_page(page_id, self, is_dirty=success)
        return success

    def write_record_at_slot(self, page_id: int, slot_id: int, data) -> bool:
        if self.buffer_pool is None:
            raise ValueError("Buffer pool is not set for this HeapFile")
        while self.get_num_pages() <= page_id:
            self.allocate_new_page()
        page = self.buffer_pool.fetch_page(page_id, self)
        success = page.write_record_at_slot(slot_id, data)
        self.buffer_pool.unpin_page(page_id, self, is_dirty=success)
        return success

    def scan_all(self) -> Iterator[Tuple[Tuple[int, int], bytes]]:
        if self.buffer_pool is None:
            raise ValueError("Buffer pool is not set for this HeapFile")

        num_pages = self.get_num_pages()
        for page_id in range(num_pages):
            page = self.buffer_pool.fetch_page(page_id, self)
            # Scan slots
            for slot_id in range(len(page.slots)):
                record = page.get_record(slot_id)
                if record is not None:
                    yield ((page_id, slot_id), record)
            self.buffer_pool.unpin_page(page_id, self, is_dirty=False)
