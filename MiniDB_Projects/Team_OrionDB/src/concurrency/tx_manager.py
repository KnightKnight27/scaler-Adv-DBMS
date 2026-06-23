import threading

class TransactionContext:
    def __init__(self, tx_id, snapshot=None):
        self.tx_id = tx_id
        self.snapshot = snapshot  # (read_tx_id, active_tx_ids_set)
        self.updates = []         # list of (page_id, before_bytes, after_bytes)
        self.is_aborted = False

class TransactionManager:
    def __init__(self, bpm, lock_manager, wal_manager, is_mvcc=False):
        self.bpm = bpm
        self.lock_manager = lock_manager
        self.wal_manager = wal_manager
        self.is_mvcc = is_mvcc
        
        self.lock = threading.Lock()
        self.next_tx_id = 1
        
        # Transaction states
        self.active_txs = set()
        self.committed_txs = set()
        self.aborted_txs = set()
        
        # tx_id -> TransactionContext
        self.transactions = {}

    def begin(self):
        with self.lock:
            tx_id = self.next_tx_id
            self.next_tx_id += 1
            self.active_txs.add(tx_id)
            
            # Capture active transaction snapshot for MVCC
            # A tuple is visible if its xmin was committed before our begin
            # active_txs snapshot should copy the currently active transactions *except* this one
            snapshot_active = set(self.active_txs)
            snapshot_active.remove(tx_id)
            snapshot = (tx_id, snapshot_active)
            
            txn_ctx = TransactionContext(tx_id, snapshot)
            self.transactions[tx_id] = txn_ctx
            
            # Log BEGIN in WAL
            if self.wal_manager:
                lsn = self.wal_manager.log_begin(tx_id)
                # We don't strictly need to force WAL to disk at BEGIN, standard ARIES rules
                
            return txn_ctx

    def commit(self, tx_id):
        with self.lock:
            if tx_id not in self.active_txs:
                return False
                
            txn_ctx = self.transactions[tx_id]
            if txn_ctx.is_aborted:
                return False
                
            # 1. Log COMMIT in WAL
            commit_lsn = 0
            if self.wal_manager:
                commit_lsn = self.wal_manager.log_commit(tx_id)
                # 2. Flush log up to commit_lsn (enforces write-ahead durability!)
                self.wal_manager.flush_to_lsn(commit_lsn)
                
            # 3. Release locks
            if self.lock_manager:
                self.lock_manager.release_locks(tx_id)
                
            # 4. Update status
            self.active_txs.remove(tx_id)
            self.committed_txs.add(tx_id)
            return True

    def abort(self, tx_id):
        with self.lock:
            if tx_id not in self.active_txs:
                return False
                
            txn_ctx = self.transactions[tx_id]
            txn_ctx.is_aborted = True
            
            # 1. Rollback / Undo updates in reverse order
            for page_id, before_bytes, after_bytes in reversed(txn_ctx.updates):
                page = self.bpm.fetch_page(page_id)
                page.data[:] = before_bytes

                # Write abort log record (CLR-like log update for rollback)
                if self.wal_manager:
                    lsn = self.wal_manager.log_update(tx_id, page_id, after_bytes, before_bytes)
                    page.set_lsn(lsn)

                self.bpm.unpin_page(page_id, is_dirty=True)
                
            # 2. Log ABORT in WAL
            if self.wal_manager:
                abort_lsn = self.wal_manager.log_abort(tx_id)
                self.wal_manager.flush_to_lsn(abort_lsn)
                
            # 3. Release locks
            if self.lock_manager:
                self.lock_manager.release_locks(tx_id)
                
            # 4. Update status
            self.active_txs.remove(tx_id)
            self.aborted_txs.add(tx_id)
            return True

    def register_update(self, tx_id, page_id, before_bytes, after_bytes):
        if tx_id == 0:
            return  # Autocommit/Non-txn write
        with self.lock:
            if tx_id in self.transactions:
                self.transactions[tx_id].updates.append((page_id, before_bytes, after_bytes))
