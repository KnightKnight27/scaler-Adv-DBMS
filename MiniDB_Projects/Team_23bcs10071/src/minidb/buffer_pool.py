from collections import OrderedDict

class BufferPool:
    def __init__(self, capacity=16):
        self.capacity = capacity
        self.pages = OrderedDict()  # page_no -> (dirty, data)

    def get(self, page_no, load_fn, write_fn=None):
        if page_no in self.pages:
            # move to end = most recently used
            self.pages.move_to_end(page_no)
            return self.pages[page_no][1]
        page = load_fn(page_no)
        if len(self.pages) >= self.capacity:
            # evict least recently used
            evict_page_no, (dirty, pdata) = self.pages.popitem(last=False)
            if dirty and write_fn:
                write_fn(evict_page_no, pdata)
        self.pages[page_no] = (False, page)
        return page

    def mark_dirty(self, page_no):
        if page_no in self.pages:
            dirty, page = self.pages[page_no]
            self.pages[page_no] = (True, page)

    def put(self, page_no, page, dirty=False):
        self.pages[page_no] = (dirty, page)
        self.pages.move_to_end(page_no)

    def flush_all(self, write_fn):
        for page_no, (dirty, page) in list(self.pages.items()):
            if dirty:
                write_fn(page_no, page)
                self.pages[page_no] = (False, page)
