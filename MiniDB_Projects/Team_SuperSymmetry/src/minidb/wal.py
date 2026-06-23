"""
Write-Ahead Logging (WAL) and crash recovery.

Logging protocol (a simplified ARIES):
  * Every modification appends an UPDATE record carrying the table, RID and
    the before/after images BEFORE the change is applied in the buffer pool.
  * The page's LSN is stamped with the record's LSN, enabling idempotent redo.
  * COMMIT forces the log to disk (fsync). Data pages follow a steal/no-force
    policy; the buffer pool enforces "flush log before data page".

Recovery performs three passes:
  1. Analysis  -- determine committed transactions and "losers".
  2. Redo      -- repeat history: re-apply every UPDATE's after-image
                  (idempotent via page LSN), reconstructing the buffer state.
  3. Undo      -- roll back losers by applying their before-images in reverse.
"""
from __future__ import annotations

import os
import pickle
import struct
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

RID = Tuple[int, int]

# record kinds
BEGIN = "BEGIN"
UPDATE = "UPDATE"
COMMIT = "COMMIT"
ABORT = "ABORT"
CHECKPOINT = "CHECKPOINT"


@dataclass
class LogRecord:
    lsn: int
    kind: str
    txn_id: Optional[int] = None
    table: Optional[str] = None
    rid: Optional[RID] = None
    before: Optional[bytes] = None
    after: Optional[bytes] = None
    active: Optional[List[int]] = None  # for checkpoint


class WALManager:
    def __init__(self, directory: str, filename: str = "wal.log"):
        self.path = os.path.join(directory, filename)
        self.records: List[LogRecord] = []
        self.flushed_upto = 0  # highest LSN durable on disk
        self._load()

    # ---- durability ------------------------------------------------------
    def _load(self):
        if not os.path.exists(self.path):
            open(self.path, "ab").close()
            return
        with open(self.path, "rb") as f:
            data = f.read()
        off = 0
        while off + 4 <= len(data):
            (ln,) = struct.unpack_from("<I", data, off)
            off += 4
            if off + ln > len(data):
                break  # torn final record -> ignore
            rec = pickle.loads(data[off : off + ln])
            self.records.append(rec)
            off += ln
        self.flushed_upto = self.records[-1].lsn if self.records else 0

    def _next_lsn(self) -> int:
        return (self.records[-1].lsn + 1) if self.records else 1

    def append(self, **kwargs) -> int:
        rec = LogRecord(lsn=self._next_lsn(), **kwargs)
        self.records.append(rec)
        return rec.lsn

    def flush(self, upto_lsn: Optional[int] = None):
        """Make the log durable up to upto_lsn (or everything)."""
        target = upto_lsn if upto_lsn is not None else (
            self.records[-1].lsn if self.records else 0
        )
        if target <= self.flushed_upto:
            return
        with open(self.path, "ab") as f:
            for rec in self.records:
                if self.flushed_upto < rec.lsn <= target:
                    blob = pickle.dumps(rec)
                    f.write(struct.pack("<I", len(blob)))
                    f.write(blob)
            f.flush()
            os.fsync(f.fileno())
        self.flushed_upto = target

    # ---- recovery --------------------------------------------------------
    def recover(self, heap_for):
        """Replay the log against heap files.

        `heap_for(table)` must return a HeapFile for the named table.
        Returns a dict of recovery statistics for demonstration.
        """
        committed = {r.txn_id for r in self.records if r.kind == COMMIT}
        aborted = {r.txn_id for r in self.records if r.kind == ABORT}
        began = {r.txn_id for r in self.records if r.kind == BEGIN}
        losers = (began - committed) - aborted

        # Pass 2: REDO (repeat history)
        redo_count = 0
        for r in self.records:
            if r.kind == UPDATE:
                heap_for(r.table).redo_set(r.rid, r.after, r.lsn)
                redo_count += 1

        # Pass 3: UNDO losers (reverse order)
        undo_count = 0
        for r in reversed(self.records):
            if r.kind == UPDATE and r.txn_id in losers:
                # apply before-image with a fresh LSN so it sticks
                new_lsn = self._next_lsn()
                heap_for(r.table).redo_set(r.rid, r.before, new_lsn)
                self.records.append(
                    LogRecord(lsn=new_lsn, kind=UPDATE, txn_id=r.txn_id,
                              table=r.table, rid=r.rid,
                              before=r.after, after=r.before)
                )
                undo_count += 1
        for tid in losers:
            self.append(kind=ABORT, txn_id=tid)
        self.flush()
        return {
            "committed": sorted(committed),
            "losers_rolled_back": sorted(losers),
            "redo_applied": redo_count,
            "undo_applied": undo_count,
        }
