import os

class DiskManager:
    PAGE_SIZE = 4096

    def __init__(self, db_filepath):
        self.db_filepath = db_filepath
        # Open in r+b mode if exists, otherwise w+b to create it
        if os.path.exists(db_filepath):
            self.file = open(db_filepath, "r+b")
        else:
            self.file = open(db_filepath, "w+b")
        self.num_pages = self._get_num_pages_on_disk()

    def _get_num_pages_on_disk(self):
        self.file.seek(0, os.SEEK_END)
        file_size = self.file.tell()
        return file_size // self.PAGE_SIZE

    def write_page(self, page_id, page_data):
        assert len(page_data) == self.PAGE_SIZE, f"Page data size must be {self.PAGE_SIZE}"
        self.file.seek(page_id * self.PAGE_SIZE)
        self.file.write(page_data)
        self.file.flush()

    def read_page(self, page_id):
        self.file.seek(page_id * self.PAGE_SIZE)
        data = self.file.read(self.PAGE_SIZE)
        if len(data) < self.PAGE_SIZE:
            # Return an empty page if read is incomplete or out of bounds
            return bytearray(self.PAGE_SIZE)
        return bytearray(data)

    def allocate_page(self):
        new_page_id = self.num_pages
        # Write an empty page to extend the file
        empty_data = bytearray(self.PAGE_SIZE)
        self.write_page(new_page_id, empty_data)
        self.num_pages += 1
        return new_page_id

    def close(self):
        if self.file and not self.file.closed:
            self.file.close()
