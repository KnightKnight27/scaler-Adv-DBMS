import threading

class WriteWriteConflict(Exception):
    pass

class Transaction:
    def __init__(self, tx_id, snapshot):
        self.tx_id = tx_id
        self.snapshot = snapshot  # set of transaction IDs active when this transaction started
        self.state = "ACTIVE"

class TransactionManager:
    def __init__(self):
        self.next_tx_id = 1
        self.active_txs = set()
        self.committed_txs = set()
        self.aborted_txs = set()
        self.lock = threading.Lock()

    def begin_tx(self):
        with self.lock:
            tx_id = self.next_tx_id
            self.next_tx_id += 1
            snapshot = set(self.active_txs)
            self.active_txs.add(tx_id)
            return Transaction(tx_id, snapshot)

    def commit_tx(self, tx_id):
        with self.lock:
            if tx_id in self.active_txs:
                self.active_txs.remove(tx_id)
                self.committed_txs.add(tx_id)

    def abort_tx(self, tx_id):
        with self.lock:
            if tx_id in self.active_txs:
                self.active_txs.remove(tx_id)
                self.aborted_txs.add(tx_id)

    def is_visible(self, xmin, xmax, tx):
        # xmin visibility check
        if xmin == 0:
            xmin_visible = True
        elif xmin == tx.tx_id:
            xmin_visible = True
        elif xmin in tx.snapshot:
            xmin_visible = False
        elif xmin in self.committed_txs:
            xmin_visible = True
        elif xmin in self.aborted_txs:
            xmin_visible = False
        else:
            # Active and not in snapshot (started after us)
            xmin_visible = False

        if not xmin_visible:
            return False

        # xmax visibility check
        if xmax == 0:
            return True
        elif xmax == tx.tx_id:
            return False
        elif xmax in tx.snapshot:
            return True
        elif xmax in self.committed_txs:
            return False
        elif xmax in self.aborted_txs:
            return True
        else:
            return True

    def check_write_conflict(self, xmax, tx):
        if xmax == 0:
            return
        if xmax == tx.tx_id:
            return
        if xmax in self.aborted_txs:
            return
        # If xmax is active or committed, we have a write-write conflict!
        raise WriteWriteConflict(f"Write-write conflict: Transaction {tx.tx_id} collided with transaction {xmax}")
