"""
Transaction manager.
Assigns monotonically increasing transaction IDs.
Tracks active and committed transactions.
"""
import threading


class Snapshot:
    """
    Snapshot of committed transactions at the moment BEGIN was called.
    Used by MVCC to determine row visibility.
    """
    def __init__(self, snapshot_xid: int, committed: frozenset[int]):
        self.snapshot_xid = snapshot_xid    # max committed xid at BEGIN time
        self.committed = committed           # set of all committed xids at BEGIN time


class Transaction:
    ACTIVE = 'ACTIVE'
    COMMITTED = 'COMMITTED'
    ABORTED = 'ABORTED'

    def __init__(self, txid: int, snapshot: Snapshot, read_only: bool = False):
        self.txid = txid
        self.snapshot = snapshot
        self.read_only = read_only   # if True, skip WAL logging
        self.state = Transaction.ACTIVE
        # track modifications for rollback
        self._undo_log: list[dict] = []  # list of undo records

    def add_undo(self, record: dict):
        self._undo_log.append(record)

    def undo_log(self) -> list[dict]:
        return list(reversed(self._undo_log))


class TransactionManager:
    def __init__(self):
        self._lock = threading.Lock()
        self._next_xid = 1
        self._active: dict[int, Transaction] = {}
        self._committed: set[int] = set()

    def begin(self, read_only: bool = False) -> Transaction:
        with self._lock:
            xid = self._next_xid
            self._next_xid += 1
            snapshot = Snapshot(
                snapshot_xid=max(self._committed, default=0),
                committed=frozenset(self._committed),
            )
            txn = Transaction(xid, snapshot, read_only=read_only)
            self._active[xid] = txn
            return txn

    def commit(self, txn: Transaction):
        with self._lock:
            txn.state = Transaction.COMMITTED
            self._committed.add(txn.txid)
            self._active.pop(txn.txid, None)

    def abort(self, txn: Transaction):
        with self._lock:
            txn.state = Transaction.ABORTED
            self._active.pop(txn.txid, None)

    def active_xids(self) -> set[int]:
        with self._lock:
            return set(self._active.keys())

    def is_committed(self, xid: int) -> bool:
        with self._lock:
            return xid in self._committed

    def restore_state(self, committed_xids: set[int], next_xid: int):
        """Called during WAL recovery to restore transaction state."""
        with self._lock:
            self._committed = set(committed_xids)
            self._next_xid = next_xid
