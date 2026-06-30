"""Write-Ahead Log (WAL) and crash recovery.

WAL invariant: the log record for a change is forced to stable storage before a
dirty page holding that change may be flushed; data pages are written lazily
(NO-FORCE), so recovery combines redo and undo. A transaction is a winner iff it
has a COMMIT record; winners are redone in log order, losers are undone in
reverse order. Undo is idempotent, so it is safe to apply whether or not the
loser's page ever reached disk.

If forcing a COMMIT record fails, the just-appended COMMIT line is truncated
before the error is surfaced, so an unacknowledged commit never becomes a winner.
Log records are newline-delimited JSON so they can be inspected by hand.
"""

import json
import os


class WriteAheadLog:
    def __init__(self, path: str):
        self.path = path
        if not os.path.exists(path):
            open(path, "w").close()
        self._f = open(path, "a+")
        self._lsn = 0

    def _append(self, record: dict) -> int:
        self._lsn += 1
        record["lsn"] = self._lsn
        self._f.write(json.dumps(record) + "\n")
        return self._lsn

    def log_begin(self, txn_id):
        self._append({"type": "BEGIN", "txn": txn_id})

    def log_insert(self, txn_id, table, key, row):
        self._append({"type": "INSERT", "txn": txn_id, "table": table,
                      "key": key, "row": list(row)})

    def log_update(self, txn_id, table, key, old, row):
        self._append({"type": "UPDATE", "txn": txn_id, "table": table,
                      "key": key, "old": list(old), "row": list(row)})

    def log_delete(self, txn_id, table, key, old):
        self._append({"type": "DELETE", "txn": txn_id, "table": table,
                      "key": key, "old": list(old)})

    def log_commit(self, txn_id):
        self._f.seek(0, os.SEEK_END)
        pos = self._f.tell()
        self._append({"type": "COMMIT", "txn": txn_id})
        try:
            self.flush()                  # WAL invariant: force log on commit
        except Exception:
            # We remove a failed COMMIT record so recovery does not treat an
            # unacknowledged transaction as durable.
            self._f.seek(pos)
            self._f.truncate(pos)
            self._f.flush()
            raise

    def log_abort(self, txn_id):
        self._append({"type": "ABORT", "txn": txn_id})

    def log_checkpoint(self):
        self._append({"type": "CHECKPOINT"})
        self.flush()

    def flush(self):
        self._f.flush()
        os.fsync(self._f.fileno())

    def truncate(self):
        """Reset the log (called after a checkpoint flushes all data)."""
        self._f.close()
        self._f = open(self.path, "w")
        self.flush()
        self._lsn = 0

    def close(self):
        if self._f is None:
            return
        try:
            self._f.flush()
            self._f.close()
        except Exception:
            pass

    def crash_close(self):
        """Release the log descriptor without flushing user-space buffers."""
        if self._f is None:
            return
        try:
            self._f.buffer.raw.close()
        except Exception:
            pass
        finally:
            self._f = None

    def read_records(self):
        with open(self.path, "r") as f:
            for line in f:
                line = line.strip()
                if line:
                    yield json.loads(line)


def recover(wal: WriteAheadLog, apply_fn, undo_fn=None):
    """Run recovery. ``apply_fn`` redoes winners; ``undo_fn`` rolls back losers.

    Returns a small report dict summarizing what was redone/undone.
    """
    records = list(wal.read_records())
    # Winners are transactions with a COMMIT after the last checkpoint.
    last_ckpt = max((i for i, r in enumerate(records)
                     if r["type"] == "CHECKPOINT"), default=-1)
    tail = records[last_ckpt + 1:]
    committed = {r["txn"] for r in tail if r["type"] == "COMMIT"}
    redone = 0
    for r in tail:
        if r["type"] in ("INSERT", "UPDATE", "DELETE") and r["txn"] in committed:
            apply_fn(r)
            redone += 1
    losers = {r["txn"] for r in tail
              if r["type"] == "BEGIN" and r["txn"] not in committed}
    undone = 0
    if undo_fn is not None:
        for r in reversed(tail):
            if r["type"] in ("INSERT", "UPDATE", "DELETE") and r["txn"] in losers:
                undo_fn(r)
                undone += 1
    return {
        "records_scanned": len(tail),
        "committed_txns": sorted(committed),
        "loser_txns": sorted(losers),
        "operations_redone": redone,
        "operations_undone": undone,
    }
