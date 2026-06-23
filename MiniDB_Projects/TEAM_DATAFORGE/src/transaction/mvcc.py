"""
MVCC — Multi-Version Concurrency Control for MiniDB (Extension Track B).

Replaces 2PL for read operations, allowing non-blocking reads while
writers still use exclusive locks for write-write conflict detection.

Key concepts:
  - Each row version has (xmin, xmax) timestamps:
    - xmin: Transaction ID that created this version
    - xmax: Transaction ID that deleted/replaced this version (0 if alive)
  - Snapshot Isolation: each transaction sees a consistent snapshot based
    on its start timestamp.
  - Visibility Rules: a version is visible to txn T if:
    1. xmin is committed and xmin < T.start_ts
    2. xmax is either 0 (not deleted) or xmax > T.start_ts or xmax is not committed

Architecture:
  - Version chains stored in a separate dict keyed by (table, pk)
  - Each entry is a list of versions sorted by creation time
  - Garbage collection removes versions invisible to all active txns
"""

import threading
from dataclasses import dataclass, field
from typing import Optional, Dict, List, Tuple, Any
from collections import defaultdict


@dataclass
class RowVersion:
    """
    A single version of a row.

    Attributes:
        data: The row data as a list of values.
        xmin: Transaction ID that created this version.
        xmax: Transaction ID that invalidated this version (0 = alive).
        xmin_committed: Whether the creating transaction has committed.
        xmax_committed: Whether the invalidating transaction has committed.
    """
    data: list
    xmin: int = 0                # Created by this txn
    xmax: int = 0                # Deleted by this txn (0 = live)
    xmin_committed: bool = False
    xmax_committed: bool = False

    def is_alive(self) -> bool:
        """Check if this version hasn't been deleted."""
        return self.xmax == 0

    def __repr__(self):
        status = 'ALIVE' if self.is_alive() else f'DELETED(xmax={self.xmax})'
        return f"RowVersion(xmin={self.xmin}, {status}, data={self.data[:3]}...)"


class MVCCManager:
    """
    Multi-Version Concurrency Control manager.

    Provides snapshot isolation for reads and coordinates with the
    lock manager for write-write conflict detection.

    Usage:
        mvcc = MVCCManager()
        mvcc.insert_version('employees', pk=1, data=[1, 'Alice', 50000], txn_id=1)
        mvcc.commit_transaction(1)

        # Transaction 2 reads a consistent snapshot
        version = mvcc.read_version('employees', pk=1, txn_id=2, snapshot_ts=2)

    Comparison with 2PL:
        2PL: readers block writers, writers block readers
        MVCC: readers never block writers, writers never block readers
        Result: higher read throughput under contention
    """

    def __init__(self):
        self._versions: Dict[Tuple[str, Any], List[RowVersion]] = defaultdict(list)
        self._lock = threading.RLock()
        self._committed_txns: set = set()  # Set of committed txn IDs
        self._active_snapshots: Dict[int, int] = {}  # txn_id -> snapshot_ts

    def begin_transaction(self, txn_id: int, snapshot_ts: int):
        """
        Register a transaction's snapshot timestamp.

        Args:
            txn_id: Transaction ID.
            snapshot_ts: The snapshot timestamp (consistent read point).
        """
        with self._lock:
            self._active_snapshots[txn_id] = snapshot_ts

    def insert_version(self, table_name: str, pk: Any, data: list, txn_id: int):
        """
        Insert a new version of a row.

        Args:
            table_name: Table name.
            pk: Primary key value.
            data: Row data as a list of values.
            txn_id: Creating transaction ID.
        """
        key = (table_name, pk)
        version = RowVersion(
            data=list(data),
            xmin=txn_id,
            xmax=0,
            xmin_committed=False,
            xmax_committed=False,
        )
        with self._lock:
            self._versions[key].append(version)

    def delete_version(self, table_name: str, pk: Any, txn_id: int) -> bool:
        """
        Mark the current version as deleted.

        Args:
            table_name: Table name.
            pk: Primary key value.
            txn_id: Deleting transaction ID.

        Returns:
            True if a version was marked as deleted.
        """
        key = (table_name, pk)
        with self._lock:
            versions = self._versions.get(key, [])
            for version in reversed(versions):
                if version.is_alive() and (version.xmin_committed or version.xmin == txn_id):
                    version.xmax = txn_id
                    return True
        return False

    def update_version(self, table_name: str, pk: Any, new_data: list, txn_id: int) -> bool:
        """
        Update a row: delete old version + insert new version.

        Args:
            table_name: Table name.
            pk: Primary key value.
            new_data: New row data.
            txn_id: Updating transaction ID.

        Returns:
            True if update succeeded.
        """
        # Mark old version as deleted
        deleted = self.delete_version(table_name, pk, txn_id)
        if not deleted:
            # No existing version to update — treat as insert
            pass

        # Insert new version
        self.insert_version(table_name, pk, new_data, txn_id)
        return True

    def read_version(self, table_name: str, pk: Any,
                      txn_id: int, snapshot_ts: int) -> Optional[list]:
        """
        Read the visible version of a row for a given transaction.

        Visibility Rules:
        A version V is visible to transaction T (with snapshot_ts S) if:
          1. V.xmin is committed AND V.xmin <= S
          2. AND either:
             a. V.xmax == 0 (not deleted), OR
             b. V.xmax is NOT committed, OR
             c. V.xmax > S (deleted after our snapshot)

        Args:
            table_name: Table name.
            pk: Primary key value.
            txn_id: Reading transaction ID.
            snapshot_ts: Snapshot timestamp.

        Returns:
            The row data if visible, None otherwise.
        """
        key = (table_name, pk)
        with self._lock:
            versions = self._versions.get(key, [])

            # Scan versions in reverse order (newest first)
            for version in reversed(versions):
                if self._is_visible(version, txn_id, snapshot_ts):
                    return list(version.data)

        return None

    def read_all_visible(self, table_name: str, txn_id: int,
                          snapshot_ts: int) -> List[Tuple[Any, list]]:
        """
        Read all visible rows in a table for a transaction.

        Args:
            table_name: Table name.
            txn_id: Transaction ID.
            snapshot_ts: Snapshot timestamp.

        Returns:
            List of (pk, row_data) tuples.
        """
        results = []
        with self._lock:
            for (tname, pk), versions in self._versions.items():
                if tname != table_name:
                    continue

                for version in reversed(versions):
                    if self._is_visible(version, txn_id, snapshot_ts):
                        results.append((pk, list(version.data)))
                        break  # Only the most recent visible version

        return results

    def _is_visible(self, version: RowVersion, txn_id: int, snapshot_ts: int) -> bool:
        """
        Check if a version is visible to a transaction.

        Implements PostgreSQL-style snapshot isolation visibility rules.
        """
        # Rule 1: If created by the current transaction and not deleted by it
        if version.xmin == txn_id:
            if version.xmax == 0:
                return True
            if version.xmax == txn_id:
                return False  # Deleted by us
            return True

        # Rule 2: xmin must be committed
        if not version.xmin_committed:
            return False

        # Rule 3: xmin must be <= snapshot timestamp
        if version.xmin > snapshot_ts:
            return False

        # Rule 4: Check deletion status
        if version.xmax == 0:
            return True  # Not deleted

        if version.xmax == txn_id:
            return False  # We deleted it

        if not version.xmax_committed:
            return True  # Deleter hasn't committed yet

        if version.xmax > snapshot_ts:
            return True  # Deleted after our snapshot

        return False  # Deleted before our snapshot by committed txn

    def commit_transaction(self, txn_id: int):
        """
        Mark a transaction as committed — makes its versions visible.

        Updates xmin_committed for all versions created by this txn,
        and xmax_committed for all versions deleted by this txn.
        """
        with self._lock:
            self._committed_txns.add(txn_id)

            for versions in self._versions.values():
                for version in versions:
                    if version.xmin == txn_id:
                        version.xmin_committed = True
                    if version.xmax == txn_id:
                        version.xmax_committed = True

            if txn_id in self._active_snapshots:
                del self._active_snapshots[txn_id]

    def abort_transaction(self, txn_id: int):
        """
        Abort a transaction — remove all its versions.

        Versions created by this txn are removed.
        Deletions by this txn are undone (xmax reset to 0).
        """
        with self._lock:
            for key in list(self._versions.keys()):
                versions = self._versions[key]
                # Remove versions created by this txn
                self._versions[key] = [v for v in versions if v.xmin != txn_id]

                # Undo deletions by this txn
                for version in self._versions[key]:
                    if version.xmax == txn_id:
                        version.xmax = 0
                        version.xmax_committed = False

            if txn_id in self._active_snapshots:
                del self._active_snapshots[txn_id]

    def garbage_collect(self):
        """
        Remove old versions that are no longer visible to any active transaction.

        A version can be garbage collected if:
          - It's been superseded by a newer committed version
          - No active transaction's snapshot can see it
        """
        with self._lock:
            if not self._active_snapshots:
                min_snapshot = float('inf')
            else:
                min_snapshot = min(self._active_snapshots.values())

            for key in list(self._versions.keys()):
                versions = self._versions[key]
                if len(versions) <= 1:
                    continue

                # Keep only versions that might be visible
                kept = []
                found_visible = False
                for version in reversed(versions):
                    if not found_visible:
                        kept.append(version)
                        if version.xmin_committed:
                            found_visible = True
                    else:
                        # Keep if any active snapshot might need it
                        if (version.xmin_committed and
                                version.xmin <= min_snapshot):
                            # An older committed version — might be needed
                            if (version.xmax_committed and
                                    version.xmax <= min_snapshot):
                                # Superseded before all active snapshots — can GC
                                continue
                        kept.append(version)

                self._versions[key] = list(reversed(kept))

    def get_version_count(self) -> int:
        """Get total number of versions across all rows."""
        with self._lock:
            return sum(len(v) for v in self._versions.values())

    def get_stats(self) -> dict:
        """Get MVCC statistics."""
        with self._lock:
            total_versions = sum(len(v) for v in self._versions.values())
            total_rows = len(self._versions)
            return {
                'total_rows': total_rows,
                'total_versions': total_versions,
                'avg_versions_per_row': (total_versions / total_rows
                                         if total_rows > 0 else 0),
                'committed_txns': len(self._committed_txns),
                'active_snapshots': len(self._active_snapshots),
            }
