"""
Write-Ahead Log (WAL).

Before any page modification, we write a log record to disk.
On crash recovery, we replay all log records for committed transactions.

Log record format (one JSON object per line):
  {"lsn": 1, "txid": 5, "op": "BEGIN"}
  {"lsn": 2, "txid": 5, "op": "INSERT", "table": "t", "page_id": 0, "slot_id": 1, "row": {...}}
  {"lsn": 3, "txid": 5, "op": "DELETE", "table": "t", "page_id": 0, "slot_id": 1, "row": {...}}
  {"lsn": 4, "txid": 5, "op": "COMMIT"}

Recovery procedure (REDO only — no UNDO for aborted txns, they just never get committed):
  1. Read log from start.
  2. Collect all committed txids.
  3. Replay INSERT/DELETE only for committed txids.
"""
import json
import os
import threading


class WAL:
    def __init__(self, path: str):
        self.path = path
        self._lock = threading.Lock()
        self._lsn = self._last_lsn() + 1
        self._file = open(path, 'a', buffering=1)  # line-buffered

    def _last_lsn(self) -> int:
        if not os.path.exists(self.path):
            return 0
        last = 0
        try:
            with open(self.path, 'r') as f:
                for line in f:
                    line = line.strip()
                    if line:
                        rec = json.loads(line)
                        last = rec.get('lsn', last)
        except Exception:
            pass
        return last

    def _write(self, record: dict):
        with self._lock:
            record['lsn'] = self._lsn
            self._lsn += 1
            self._file.write(json.dumps(record) + '\n')
            self._file.flush()
            os.fsync(self._file.fileno())

    # ── log operations ────────────────────────────────────────────────────────

    def log_begin(self, txid: int):
        self._write({'txid': txid, 'op': 'BEGIN'})

    def log_insert(self, txid: int, table: str, page_id: int, slot_id: int, row: dict):
        self._write({'txid': txid, 'op': 'INSERT',
                     'table': table, 'page_id': page_id, 'slot_id': slot_id, 'row': row})

    def log_delete(self, txid: int, table: str, page_id: int, slot_id: int, row: dict):
        self._write({'txid': txid, 'op': 'DELETE',
                     'table': table, 'page_id': page_id, 'slot_id': slot_id, 'row': row})

    def log_commit(self, txid: int):
        self._write({'txid': txid, 'op': 'COMMIT'})

    def log_abort(self, txid: int):
        self._write({'txid': txid, 'op': 'ABORT'})

    def close(self):
        self._file.close()

    # ── recovery ─────────────────────────────────────────────────────────────

    def recover(self) -> tuple[list[dict], set[int]]:
        """
        Read log and return (redo_records, committed_txids).
        Caller applies redo_records in order for committed txids only.
        """
        if not os.path.exists(self.path):
            return [], set()

        records = []
        committed = set()
        aborted = set()

        with open(self.path, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError:
                    continue
                op = rec.get('op')
                txid = rec.get('txid')
                if op == 'COMMIT':
                    committed.add(txid)
                elif op == 'ABORT':
                    aborted.add(txid)
                elif op in ('INSERT', 'DELETE'):
                    records.append(rec)

        redo = [r for r in records if r['txid'] in committed]
        return redo, committed
