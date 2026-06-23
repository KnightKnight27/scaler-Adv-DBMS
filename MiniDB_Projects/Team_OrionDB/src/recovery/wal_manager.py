import os
import struct

class LogRecordType:
    BEGIN = 1
    COMMIT = 2
    ABORT = 3
    UPDATE = 4

class WALManager:
    def __init__(self, log_filepath):
        self.log_filepath = log_filepath
        self.file = open(log_filepath, "a+b")
        self.next_lsn = self._get_next_lsn()
        self.flushed_lsn = 0

    def _get_next_lsn(self):
        self.file.seek(0, os.SEEK_END)
        size = self.file.tell()
        # Each record has an LSN stored inside it, or we can just use file offset as the LSN!
        # Using file offset as LSN is a classic, elegant trick:
        # 1. It naturally guarantees LSNs are monotonically increasing.
        # 2. It gives the exact file offset where the record begins, making random access during recovery trivial!
        return size

    def log_begin(self, txn_id):
        lsn = self.next_lsn
        # Format: lsn (8B), type (1B), txn_id (4B)
        record = struct.pack(">QBI", lsn, LogRecordType.BEGIN, txn_id)
        self.file.write(record)
        self.file.flush()
        self.next_lsn = self.file.tell()
        return lsn

    def log_commit(self, txn_id):
        lsn = self.next_lsn
        record = struct.pack(">QBI", lsn, LogRecordType.COMMIT, txn_id)
        self.file.write(record)
        self.file.flush()
        self.next_lsn = self.file.tell()
        return lsn

    def log_abort(self, txn_id):
        lsn = self.next_lsn
        record = struct.pack(">QBI", lsn, LogRecordType.ABORT, txn_id)
        self.file.write(record)
        self.file.flush()
        self.next_lsn = self.file.tell()
        return lsn

    def log_update(self, txn_id, page_id, before_bytes, after_bytes):
        lsn = self.next_lsn
        length = len(before_bytes)
        assert len(after_bytes) == length, "Before and after bytes must be of equal length"

        # Full-page logging: before_bytes and after_bytes are complete 4096-byte page images.
        # Header format: lsn (8B), type (1B), txn_id (4B), page_id (4B), length (2B)
        header = struct.pack(">QBIIH", lsn, LogRecordType.UPDATE, txn_id, page_id, length)
        record = header + before_bytes + after_bytes

        self.file.write(record)
        self.file.flush()
        self.next_lsn = self.file.tell()
        return lsn

    def flush_to_lsn(self, lsn):
        if lsn > self.flushed_lsn:
            self.file.flush()
            try:
                os.fsync(self.file.fileno())
            except OSError:
                pass
            self.flushed_lsn = lsn

    def close(self):
        if self.file and not self.file.closed:
            self.file.close()
