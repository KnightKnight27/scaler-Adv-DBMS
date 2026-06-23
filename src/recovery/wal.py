"""
Write-Ahead Log (WAL) — Append-only log for crash recovery in MiniDB.

Every modification is logged BEFORE the actual change is applied.
The WAL ensures durability: committed transactions survive crashes.

Log Record Format:
  [LSN: 8B] [txn_id: 4B] [type: 1B] [data_len: 4B] [data: variable]

Log Record Types:
  - BEGIN:      Transaction started
  - INSERT:     Record inserted (redo info)
  - DELETE:     Record deleted (redo/undo info)
  - UPDATE:     Record updated (before/after images)
  - COMMIT:     Transaction committed
  - ABORT:      Transaction aborted
  - CHECKPOINT: Database state snapshot marker
"""

import os
import struct
import json
import threading
from enum import IntEnum
from dataclasses import dataclass, field
from typing import Optional, List


class LogRecordType(IntEnum):
    BEGIN = 1
    INSERT = 2
    DELETE = 3
    UPDATE = 4
    COMMIT = 5
    ABORT = 6
    CHECKPOINT = 7


# Log record header: [LSN: 8B] [txn_id: 4B] [type: 1B] [data_len: 4B]
LOG_HEADER_FORMAT = '!QIbI'
LOG_HEADER_SIZE = struct.calcsize(LOG_HEADER_FORMAT)  # 17 bytes


@dataclass
class LogRecord:
    """A single WAL log record."""
    lsn: int = 0
    txn_id: int = 0
    record_type: LogRecordType = LogRecordType.BEGIN
    data: dict = field(default_factory=dict)

    def serialize(self) -> bytes:
        """Serialize the log record to bytes."""
        data_json = json.dumps(self.data, default=str).encode('utf-8')
        header = struct.pack(LOG_HEADER_FORMAT, self.lsn, self.txn_id,
                             self.record_type.value, len(data_json))
        return header + data_json

    @staticmethod
    def deserialize(raw: bytes) -> Optional['LogRecord']:
        """Deserialize a log record from bytes."""
        if len(raw) < LOG_HEADER_SIZE:
            return None

        lsn, txn_id, rec_type, data_len = struct.unpack_from(LOG_HEADER_FORMAT, raw, 0)

        if len(raw) < LOG_HEADER_SIZE + data_len:
            return None

        data_json = raw[LOG_HEADER_SIZE:LOG_HEADER_SIZE + data_len]
        try:
            data = json.loads(data_json.decode('utf-8'))
        except (json.JSONDecodeError, UnicodeDecodeError):
            data = {}

        return LogRecord(
            lsn=lsn,
            txn_id=txn_id,
            record_type=LogRecordType(rec_type),
            data=data,
        )

    @property
    def total_size(self) -> int:
        """Total serialized size."""
        return LOG_HEADER_SIZE + len(json.dumps(self.data, default=str).encode('utf-8'))

    def __repr__(self):
        return (f"LogRecord(lsn={self.lsn}, txn={self.txn_id}, "
                f"type={self.record_type.name}, data_keys={list(self.data.keys())})")


class WAL:
    """
    Write-Ahead Log — append-only log file for crash recovery.

    All modifications are written to the WAL before being applied
    to data pages. The WAL is force-flushed on COMMIT to ensure
    durability.

    Usage:
        wal = WAL('/path/to/db')
        lsn = wal.append(LogRecord(txn_id=1, record_type=LogRecordType.BEGIN))
        wal.flush()
        records = wal.read_all()

    Thread Safety:
        All methods are thread-safe.
    """

    def __init__(self, db_dir: str, filename: str = '_wal.log'):
        """
        Args:
            db_dir: Directory for the WAL file.
            filename: Name of the WAL file.
        """
        self.db_dir = db_dir
        self.filepath = os.path.join(db_dir, filename)
        self._lock = threading.Lock()
        self._next_lsn = 1
        self._fh = None

        os.makedirs(db_dir, exist_ok=True)

        # Recover LSN from existing log
        if os.path.exists(self.filepath):
            records = self.read_all()
            if records:
                self._next_lsn = records[-1].lsn + 1

    def _get_fh(self):
        """Get file handle, opening if needed."""
        if self._fh is None:
            self._fh = open(self.filepath, 'ab+')
        return self._fh

    def append(self, record: LogRecord) -> int:
        """
        Append a log record to the WAL.

        Args:
            record: The log record to append.

        Returns:
            The LSN assigned to this record.
        """
        with self._lock:
            record.lsn = self._next_lsn
            self._next_lsn += 1

            fh = self._get_fh()
            data = record.serialize()
            # Write length prefix for easy reading
            fh.write(struct.pack('!I', len(data)))
            fh.write(data)

            return record.lsn

    def flush(self):
        """Force WAL to disk (fsync)."""
        with self._lock:
            if self._fh:
                self._fh.flush()
                try:
                    os.fsync(self._fh.fileno())
                except OSError:
                    pass

    def read_all(self) -> List[LogRecord]:
        """
        Read all log records from the WAL file.

        Returns:
            List of LogRecord objects in order.
        """
        records = []
        if not os.path.exists(self.filepath):
            return records

        with open(self.filepath, 'rb') as f:
            while True:
                # Read length prefix
                len_bytes = f.read(4)
                if len(len_bytes) < 4:
                    break

                record_len = struct.unpack('!I', len_bytes)[0]
                record_data = f.read(record_len)
                if len(record_data) < record_len:
                    break

                record = LogRecord.deserialize(record_data)
                if record:
                    records.append(record)

        return records

    def truncate(self):
        """Clear the WAL (after successful checkpoint)."""
        with self._lock:
            if self._fh:
                self._fh.close()
                self._fh = None
            # Overwrite with empty file
            with open(self.filepath, 'wb') as f:
                pass
            self._next_lsn = 1

    def get_size(self) -> int:
        """Get the WAL file size in bytes."""
        if os.path.exists(self.filepath):
            return os.path.getsize(self.filepath)
        return 0

    def close(self):
        """Close the WAL file handle."""
        with self._lock:
            if self._fh:
                self._fh.close()
                self._fh = None
