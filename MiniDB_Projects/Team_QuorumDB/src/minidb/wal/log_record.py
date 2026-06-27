"""Write-ahead log records.

Every change to the database is described by a physiological log record:
*which page* changed, *which slot*, and the before/after byte images needed to
undo/redo it. Because records are page-level and self-describing, the very same
encoding drives two subsystems:

* **Crash recovery** replays records against local pages (ARIES redo/undo).
* **Replication (Track D)** ships records over the wire; the replica applies
  the redo image to its own copy of the page.

Record types:
    BEGIN / COMMIT / ABORT   transaction boundaries
    INSERT / DELETE / UPDATE  data changes (carry before/after images)
    CLR                       compensation record written during undo
    CHECKPOINT                marks a point where all dirty pages were flushed
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional


class LogType(IntEnum):
    BEGIN = 1
    COMMIT = 2
    ABORT = 3
    INSERT = 4
    DELETE = 5
    UPDATE = 6
    CLR = 7
    CHECKPOINT = 8


_HEAD = struct.Struct("<QqqBIiiHII")
# lsn, prev_lsn, undo_next_lsn, type, txn_id, page_id, slot_no,
# table_len, before_len, after_len


@dataclass
class LogRecord:
    lsn: int = 0
    type: LogType = LogType.BEGIN
    txn_id: int = 0
    prev_lsn: int = -1
    undo_next_lsn: int = -1
    table: str = ""
    page_id: int = -1
    slot_no: int = -1
    before: bytes = b""
    after: bytes = b""

    def encode(self) -> bytes:
        tbl = self.table.encode("utf-8")
        head = _HEAD.pack(
            self.lsn, self.prev_lsn, self.undo_next_lsn, int(self.type),
            self.txn_id, self.page_id, self.slot_no,
            len(tbl), len(self.before), len(self.after),
        )
        return head + tbl + self.before + self.after

    @classmethod
    def decode_from(cls, buf: bytes, offset: int = 0) -> tuple["LogRecord", int]:
        """Decode one record starting at *offset*; return (record, next_offset)."""
        (lsn, prev_lsn, undo_next, typ, txn_id, page_id, slot_no,
         tlen, blen, alen) = _HEAD.unpack_from(buf, offset)
        pos = offset + _HEAD.size
        table = buf[pos:pos + tlen].decode("utf-8"); pos += tlen
        before = bytes(buf[pos:pos + blen]); pos += blen
        after = bytes(buf[pos:pos + alen]); pos += alen
        rec = cls(lsn=lsn, type=LogType(typ), txn_id=txn_id, prev_lsn=prev_lsn,
                  undo_next_lsn=undo_next, table=table, page_id=page_id,
                  slot_no=slot_no, before=before, after=after)
        return rec, pos

    def framed(self) -> bytes:
        """Length-framed encoding for streaming (file/network)."""
        body = self.encode()
        return struct.pack("<I", len(body)) + body

    def __repr__(self) -> str:  # pragma: no cover - cosmetic
        return (f"<Log lsn={self.lsn} {self.type.name} txn={self.txn_id} "
                f"tbl={self.table!r} pg={self.page_id} slot={self.slot_no}>")
