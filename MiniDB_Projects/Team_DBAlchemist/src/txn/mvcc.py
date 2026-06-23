"""
MVCC (Multi-Version Concurrency Control).

Every row dict stored on disk has two hidden system fields:
  _xmin: txid that created this row version
  _xmax: txid that deleted this row version (None = row is alive)

Visibility rule (snapshot isolation):
  A row is visible to transaction T (with snapshot S) if:
    1. xmin was committed before the snapshot was taken
       i.e., xmin in S.committed (xmin committed at or before BEGIN)
    2. AND the row has not been deleted, OR was deleted by a transaction
       that was NOT yet committed when T's snapshot was taken.
       i.e., xmax is None, OR xmax NOT in S.committed

This gives readers a consistent snapshot without locking.
Writers create new row versions instead of updating in place.
"""

from txn.transaction import Transaction, Snapshot, TransactionManager


class MVCCManager:
    def __init__(self, txn_manager: TransactionManager):
        self.txn_manager = txn_manager

    # ── row factory ──────────────────────────────────────────────────────────

    def new_row(self, data: dict, txid: int) -> dict:
        """Wrap user data with MVCC system fields."""
        return {'_xmin': txid, '_xmax': None, **data}

    def mark_deleted(self, row: dict, txid: int) -> dict:
        """Return a copy of the row with _xmax set (logical delete)."""
        return {**row, '_xmax': txid}

    # ── visibility ───────────────────────────────────────────────────────────

    def is_visible(self, row: dict, snapshot: Snapshot) -> bool:
        """
        Check if row is visible to a transaction with the given snapshot.

        xmin check: row was created by a committed transaction
                    that committed before this snapshot.
        xmax check: row is not deleted, or deleted by a transaction
                    that had not yet committed when snapshot was taken.
        """
        xmin = row.get('_xmin')
        xmax = row.get('_xmax')

        # xmin must be committed and visible in our snapshot
        if xmin not in snapshot.committed:
            return False

        # row is alive (not deleted)
        if xmax is None:
            return True

        # row deleted — visible only if deleter's txn was NOT committed at snapshot time
        return xmax not in snapshot.committed

    def is_visible_to_txn(self, row: dict, txn: Transaction) -> bool:
        return self.is_visible(row, txn.snapshot)

    # ── write conflict detection ──────────────────────────────────────────────

    def can_write(self, row: dict, txn: Transaction) -> bool:
        """
        Check no concurrent transaction has already modified this row.
        (First-writer-wins: if another active txn modified this row, abort.)
        """
        xmax = row.get('_xmax')
        if xmax is None:
            return True
        # if deleter is active (not committed, not self), there's a write conflict
        if xmax == txn.txid:
            return True  # we deleted it ourselves
        active = self.txn_manager.active_xids()
        if xmax in active:
            return False  # concurrent writer — conflict
        return True

    # ── strip system fields ──────────────────────────────────────────────────

    @staticmethod
    def strip_system_fields(row: dict) -> dict:
        return {k: v for k, v in row.items() if not k.startswith('_')}
