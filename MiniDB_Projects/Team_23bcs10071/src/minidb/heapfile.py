import os
from .page import Page, PAGE_SIZE

class HeapFile:
    def __init__(self, path):
        self.path = path
        if not os.path.exists(path):
            # create empty file
            open(path, 'wb').close()

    def num_pages(self):
        size = os.path.getsize(self.path)
        return size // PAGE_SIZE

    def read_page(self, page_no):
        with open(self.path, 'rb') as f:
            f.seek(page_no * PAGE_SIZE)
            data = f.read(PAGE_SIZE)
            if len(data) < PAGE_SIZE:
                data = data + bytes(PAGE_SIZE - len(data))
            return Page(data)

    def append_page(self, page: Page):
        with open(self.path, 'ab') as f:
            f.write(page.data)
        return self.num_pages() - 1

    def write_page(self, page_no, page: Page):
        with open(self.path, 'r+b') as f:
            f.seek(page_no * PAGE_SIZE)
            f.write(page.data)
