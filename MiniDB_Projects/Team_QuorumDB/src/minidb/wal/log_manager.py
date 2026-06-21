"""Log manager: assigns LSNs, appends records, and enforces durability.

The log is an append-only, length-framed sequence of records on disk plus an
in-memory mirror (cheap for teaching-scale workloads) that recovery and the
replication primary read from.

Durability contract:
* ``append`` assigns a monotonically increasing LSN and buffers the record.
* ``flush(lsn)`` makes the log durable up to *lsn* (fsync). The buffer pool
  calls this before writing any dirty page (the write-ahead rule), and a
  transaction calls it on COMMIT so a committed transaction survives a crash.
"""

from __future__ import annotations

import os
import struct
import threading
from typing import List, Optional

from .log_record import LogRecord, LogType

_FRAME = struct.Struct("<I")


class LogManager:
    def __init__(self, wal_path: str):
        self.wal_path = wal_path
        self._lock = threading.RLock()
        self._records: List[LogRecord] = []   # in-memory mirror, ordered by LSN
        self._unflushed: List[bytes] = []
        self._next_lsn = 1
        self.flushed_lsn = 0
        if not os.path.exists(wal_path):
            parent = os.path.dirname(wal_path)
            if parent:
                os.makedirs(parent, exist_ok=True)
            open(wal_path, "wb").close()
        self._f = open(wal_path, "r+b", buffering=0)
        self._load()

    def _load(self) -> None:
        self._f.seek(0)
        data = self._f.read()
        off = 0
        while off + _FRAME.size <= len(data):
            (length,) = _FRAME.unpack_from(data, off)
            off += _FRAME.size
            if off + length > len(data):
                break  # torn tail record from a crash mid-write; ignore it
            rec, _ = LogRecord.decode_from(data, off)
            off += length
            self._records.append(rec)
        if self._records:
            self._next_lsn = self._records[-1].lsn + 1
            self.flushed_lsn = self._records[-1].lsn
        self._f.seek(0, os.SEEK_END)

    # -- append / flush -----------------------------------------------------
    def append(self, record: LogRecord) -> int:
        with self._lock:
            record.lsn = self._next_lsn
            self._next_lsn += 1
            self._records.append(record)
            self._unflushed.append(record.framed())
            return record.lsn

    def flush(self, upto_lsn: Optional[int] = None) -> None:
        """Make the log durable. Sequential append means flushing the buffer
        guarantees everything up to and including *upto_lsn* is on disk."""
        with self._lock:
            if upto_lsn is not None and upto_lsn <= self.flushed_lsn:
                return
            if not self._unflushed:
                return
            self._f.write(b"".join(self._unflushed))
            self._f.flush()
            os.fsync(self._f.fileno())
            self._unflushed.clear()
            self.flushed_lsn = self._records[-1].lsn

    # -- reads --------------------------------------------------------------
    def records(self) -> List[LogRecord]:
        with self._lock:
            return list(self._records)

    def records_since(self, lsn: int) -> List[LogRecord]:
        """Records with LSN strictly greater than *lsn* (used by replication)."""
        with self._lock:
            return [r for r in self._records if r.lsn > lsn]

    def last_checkpoint_lsn(self) -> int:
        with self._lock:
            for r in reversed(self._records):
                if r.type is LogType.CHECKPOINT:
                    return r.lsn
            return 0

    @property
    def current_lsn(self) -> int:
        with self._lock:
            return self._next_lsn - 1

    def close(self) -> None:
        with self._lock:
            self.flush()
            if not self._f.closed:
                self._f.close()
