"""
Recovery Manager — ARIES-style crash recovery for MiniDB.

Implements a simplified version of the ARIES recovery protocol:
  1. Analysis Phase: Scan the log to identify active transactions at crash time
  2. Redo Phase: Replay all committed operations to restore database state
  3. Undo Phase: Rollback all uncommitted transactions

The recovery manager also provides logging methods used during normal
operation to record modifications to the WAL.
"""

from typing import Optional, Dict, Set
from .wal import WAL, LogRecord, LogRecordType


class RecoveryManager:
    """
    ARIES-style Recovery Manager.

    During normal operation:
      - Logs all modifications (INSERT, DELETE, UPDATE) to the WAL
      - Logs transaction boundaries (BEGIN, COMMIT, ABORT)
      - Provides checkpoint functionality

    During crash recovery:
      - Analyzes the log to determine database state
      - Redoes committed operations
      - Undoes uncommitted operations

    Usage:
        rm = RecoveryManager(wal)
        rm.log_begin(txn_id=1)
        rm.log_insert(txn_id=1, table='employees', values=[1, 'Alice'])
        rm.log_commit(txn_id=1)
        rm.flush()

        # After crash:
        committed, aborted = rm.recover()
    """

    def __init__(self, wal: WAL):
        self.wal = wal

    # ─── Logging Methods (Normal Operation) ──────────────────────────

    def log_begin(self, txn_id: int) -> int:
        """Log a BEGIN record."""
        record = LogRecord(
            txn_id=txn_id,
            record_type=LogRecordType.BEGIN,
            data={},
        )
        return self.wal.append(record)

    def log_insert(self, txn_id: int, table_name: str, values: list) -> int:
        """Log an INSERT record."""
        record = LogRecord(
            txn_id=txn_id,
            record_type=LogRecordType.INSERT,
            data={
                'table': table_name,
                'values': values,
            },
        )
        return self.wal.append(record)

    def log_delete(self, txn_id: int, table_name: str, rid) -> int:
        """Log a DELETE record."""
        record = LogRecord(
            txn_id=txn_id,
            record_type=LogRecordType.DELETE,
            data={
                'table': table_name,
                'rid': str(rid),
            },
        )
        return self.wal.append(record)

    def log_update(self, txn_id: int, table_name: str, rid,
                    old_values: list, new_values: list) -> int:
        """Log an UPDATE record with before and after images."""
        record = LogRecord(
            txn_id=txn_id,
            record_type=LogRecordType.UPDATE,
            data={
                'table': table_name,
                'rid': str(rid),
                'old_values': old_values,
                'new_values': new_values,
            },
        )
        return self.wal.append(record)

    def log_commit(self, txn_id: int) -> int:
        """Log a COMMIT record."""
        record = LogRecord(
            txn_id=txn_id,
            record_type=LogRecordType.COMMIT,
            data={},
        )
        return self.wal.append(record)

    def log_abort(self, txn_id: int) -> int:
        """Log an ABORT record."""
        record = LogRecord(
            txn_id=txn_id,
            record_type=LogRecordType.ABORT,
            data={},
        )
        return self.wal.append(record)

    def log_checkpoint(self, active_txns: set) -> int:
        """Log a CHECKPOINT record."""
        record = LogRecord(
            txn_id=0,
            record_type=LogRecordType.CHECKPOINT,
            data={
                'active_txns': list(active_txns),
            },
        )
        return self.wal.append(record)

    def flush(self):
        """Force WAL to disk."""
        self.wal.flush()

    def checkpoint(self, active_txns: set):
        """
        Create a checkpoint: log checkpoint record and flush.

        A checkpoint marks a point where all dirty pages have been
        flushed to disk, reducing recovery time.
        """
        self.log_checkpoint(active_txns)
        self.flush()

    # ─── Recovery (After Crash) ──────────────────────────────────────

    def recover(self) -> tuple:
        """
        Perform ARIES-style crash recovery.

        Returns:
            (committed_txns, aborted_txns) — sets of transaction IDs.

        The three phases:
          1. Analysis: determine which transactions committed vs were active
          2. Redo: replay committed operations
          3. Undo: rollback uncommitted operations
        """
        records = self.wal.read_all()
        if not records:
            return (set(), set())

        # ─── Phase 1: Analysis ────────────────────────────────────
        committed_txns, active_txns, txn_records = self._analysis_phase(records)

        # ─── Phase 2: Redo ────────────────────────────────────────
        redo_operations = self._redo_phase(records, committed_txns)

        # ─── Phase 3: Undo ────────────────────────────────────────
        undo_operations = self._undo_phase(records, active_txns)

        return (committed_txns, active_txns)

    def _analysis_phase(self, records: list) -> tuple:
        """
        Analysis Phase: scan the log to categorize transactions.

        Returns:
            (committed_txns, active_txns, txn_records)
        """
        committed_txns: Set[int] = set()
        aborted_txns: Set[int] = set()
        all_txns: Set[int] = set()
        txn_records: Dict[int, list] = {}

        for record in records:
            txn_id = record.txn_id
            if txn_id == 0:
                continue  # Checkpoint records

            all_txns.add(txn_id)
            if txn_id not in txn_records:
                txn_records[txn_id] = []
            txn_records[txn_id].append(record)

            if record.record_type == LogRecordType.COMMIT:
                committed_txns.add(txn_id)
            elif record.record_type == LogRecordType.ABORT:
                aborted_txns.add(txn_id)

        # Active (uncommitted, unaborted) transactions at crash time
        active_txns = all_txns - committed_txns - aborted_txns

        return (committed_txns, active_txns, txn_records)

    def _redo_phase(self, records: list, committed_txns: set) -> list:
        """
        Redo Phase: collect operations from committed transactions
        that need to be replayed.

        Returns:
            List of (record_type, data) for committed operations.
        """
        redo_ops = []
        for record in records:
            if record.txn_id in committed_txns:
                if record.record_type in (LogRecordType.INSERT,
                                           LogRecordType.DELETE,
                                           LogRecordType.UPDATE):
                    redo_ops.append(record)
        return redo_ops

    def _undo_phase(self, records: list, active_txns: set) -> list:
        """
        Undo Phase: collect operations from uncommitted transactions
        that need to be rolled back (in reverse order).

        Returns:
            List of records to undo (in reverse chronological order).
        """
        undo_ops = []
        for record in reversed(records):
            if record.txn_id in active_txns:
                if record.record_type in (LogRecordType.INSERT,
                                           LogRecordType.DELETE,
                                           LogRecordType.UPDATE):
                    undo_ops.append(record)
        return undo_ops

    def get_committed_data(self) -> dict:
        """
        After recovery, get all committed operations organized by table.

        Returns:
            Dict of {table_name: {'inserts': [...], 'deletes': [...], 'updates': [...]}}.
        """
        records = self.wal.read_all()
        committed_txns, _, _ = self._analysis_phase(records)

        result: Dict[str, dict] = {}

        for record in records:
            if record.txn_id not in committed_txns:
                continue

            table = record.data.get('table', '')
            if table not in result:
                result[table] = {'inserts': [], 'deletes': [], 'updates': []}

            if record.record_type == LogRecordType.INSERT:
                result[table]['inserts'].append(record.data.get('values', []))
            elif record.record_type == LogRecordType.DELETE:
                result[table]['deletes'].append(record.data.get('rid', ''))
            elif record.record_type == LogRecordType.UPDATE:
                result[table]['updates'].append({
                    'rid': record.data.get('rid', ''),
                    'old': record.data.get('old_values', []),
                    'new': record.data.get('new_values', []),
                })

        return result

    def get_log_summary(self) -> dict:
        """Get a summary of the WAL contents."""
        records = self.wal.read_all()
        committed, active, _ = self._analysis_phase(records)

        type_counts = {}
        for record in records:
            name = record.record_type.name
            type_counts[name] = type_counts.get(name, 0) + 1

        return {
            'total_records': len(records),
            'record_types': type_counts,
            'committed_txns': len(committed),
            'active_txns_at_crash': len(active),
            'wal_size_bytes': self.wal.get_size(),
        }
