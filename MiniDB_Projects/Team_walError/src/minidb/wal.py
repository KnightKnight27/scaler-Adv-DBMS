"""wal.py — write-ahead log + redo recovery (the durability core).

MiniDB treats the WAL as the source of truth (see the project design notes). Each
logged record is one JSON object on its own line (newline-delimited), which makes
the log easy to append, fsync, and inspect by eye.

Record shapes (all carry a "txn" id):
    {"op": "begin",        "txn": t}
    {"op": "create_table", "txn": t, "table": name, "columns": [[name,type,nullable,pk], ...]}
    {"op": "create_index", "txn": t, "table": name, "column": c}
    {"op": "insert",       "txn": t, "table": name, "values": [...]}
    {"op": "delete",       "txn": t, "table": name, "values": [...]}   # full row, for replay
    {"op": "commit",       "txn": t}
    {"op": "abort",        "txn": t}

Durability rule: append the record describing a change BEFORE applying it; on
commit, append COMMIT and fsync. Recovery replays only the operations of
transactions that have a COMMIT record, in log order, onto a fresh catalog —
uncommitted work is never replayed, so it disappears after a crash with no UNDO.
"""

from __future__ import annotations

import json
import os
from typing import Any

from .catalog import Catalog
from .types import Column, ColumnType, Schema


class WriteAheadLog:
    def __init__(self, path: str = ":memory:") -> None:
        self.in_memory = path == ":memory:"
        self._records: list[dict] = []
        self._f = None
        if not self.in_memory:
            self.wal_path = path + ".wal"
            # read any existing records first (for recovery), then open to append
            if os.path.exists(self.wal_path):
                with open(self.wal_path, "r", encoding="utf-8") as fh:
                    for line in fh:
                        line = line.strip()
                        if line:
                            self._records.append(json.loads(line))
            self._f = open(self.wal_path, "a", encoding="utf-8")

    def append(self, record: dict) -> None:
        """Append one record (kept in memory; written to file if persistent)."""
        self._records.append(record)
        if self._f is not None:
            self._f.write(json.dumps(record) + "\n")

    def flush(self) -> None:
        """Force the log to durable storage (the WAL durability point)."""
        if self._f is not None:
            self._f.flush()
            os.fsync(self._f.fileno())

    def read_all(self) -> list[dict]:
        return list(self._records)

    def close(self) -> None:
        if self._f is not None:
            self.flush()
            self._f.close()
            self._f = None


# --- recovery ----------------------------------------------------------------


def _schema_from_columns(cols: list[list[Any]]) -> Schema:
    return Schema(
        [Column(name, ColumnType(t), nullable=nul, primary_key=pk)
         for (name, t, nul, pk) in cols]
    )


def replay(records: list[dict], catalog: Catalog) -> dict[str, int]:
    """Replay the committed transactions in `records` into `catalog`.

    Returns a small stats dict for logging/demos.
    """
    committed = {r["txn"] for r in records if r["op"] == "commit"}
    stats = {"committed_txns": len(committed), "applied": 0, "skipped": 0}
    for r in records:
        op = r["op"]
        if op in ("begin", "commit", "abort"):
            continue
        if r["txn"] not in committed:
            stats["skipped"] += 1
            continue
        _apply(r, catalog)
        stats["applied"] += 1
    return stats


def _apply(r: dict, catalog: Catalog) -> None:
    op = r["op"]
    if op == "create_table":
        catalog.create_table(r["table"], _schema_from_columns(r["columns"]))
    elif op == "create_index":
        catalog.get_table(r["table"]).create_index(r["column"])
    elif op == "insert":
        catalog.get_table(r["table"]).insert_row(tuple(r["values"]))
    elif op == "delete":
        catalog.get_table(r["table"]).delete_by_values(tuple(r["values"]))
    else:  # pragma: no cover - defensive
        raise ValueError(f"unknown WAL op during replay: {op!r}")
