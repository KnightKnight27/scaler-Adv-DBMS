from src.concurrency.mvcc import is_visible
from src.storage.page import Page

class TransactionAbortException(Exception):
    pass

class Operator:
    def init(self):
        pass
    def next(self):
        pass
    def close(self):
        pass

class SeqScanOperator(Operator):
    def __init__(self, bpm, table_name, schema, first_page_id, txn_id=None, lock_manager=None, is_mvcc=False, read_snapshot=None, committed_txs=None):
        self.bpm = bpm
        self.table_name = table_name
        self.schema = schema
        self.first_page_id = first_page_id
        self.txn_id = txn_id
        self.lock_manager = lock_manager
        self.is_mvcc = is_mvcc
        
        # MVCC read snapshot elements
        self.active_tx_ids = read_snapshot[1] if read_snapshot else set()
        self.read_tx_id = read_snapshot[0] if read_snapshot else (txn_id if txn_id else 0)
        self.committed_txs = committed_txs if committed_txs else set()

        self.curr_page_id = self.first_page_id
        self.curr_page = None
        self.curr_slot_idx = 0

    def init(self):
        if self.curr_page:
            self.bpm.unpin_page(self.curr_page.get_page_id(), is_dirty=False)
            self.curr_page = None
        self.curr_page_id = self.first_page_id
        self.curr_slot_idx = 0

    def next(self):
        while self.curr_page_id != 0xFFFFFFFF:
            if self.curr_page is None:
                self.curr_page = self.bpm.fetch_page(self.curr_page_id)
                self.curr_slot_idx = 0
            
            num_slots = self.curr_page.get_num_slots()
            while self.curr_slot_idx < num_slots:
                slot_idx = self.curr_slot_idx
                self.curr_slot_idx += 1
                
                record = self.curr_page.get_record(slot_idx)
                if record is not None:
                    xmin, xmax, payload = record
                    
                    # 1. MVCC Visibility check
                    visible = is_visible(xmin, xmax, self.read_tx_id, self.active_tx_ids, self.committed_txs, self.is_mvcc)
                    if visible:
                        rid = (self.curr_page_id, slot_idx)
                        # 2. SS2PL Lock Acquisition
                        if not self.is_mvcc and self.txn_id is not None and self.lock_manager is not None:
                            # Acquire S Lock
                            success = self.lock_manager.acquire_shared(self.txn_id, rid)
                            if not success:
                                raise TransactionAbortException(f"Txn {self.txn_id} aborted due to deadlock or lock timeout during SeqScan")
                        
                        # Unpack tuple
                        values = self.schema.unpack(payload)
                        # Build dictionary
                        row = {}
                        for col, val in zip(self.schema.columns, values):
                            row[col] = val
                            row[f"{self.table_name}.{col}"] = val
                        row["_rid"] = rid
                        return row
            
            # Move to next page
            next_page_id = self.curr_page.get_next_page_id()
            self.bpm.unpin_page(self.curr_page_id, is_dirty=False)
            self.curr_page = None
            self.curr_page_id = next_page_id

        return None

    def close(self):
        if self.curr_page:
            self.bpm.unpin_page(self.curr_page.get_page_id(), is_dirty=False)
            self.curr_page = None


class IndexScanOperator(Operator):
    def __init__(self, bpm, table_name, schema, bplus_tree, key, txn_id=None, lock_manager=None, is_mvcc=False, read_snapshot=None, committed_txs=None):
        self.bpm = bpm
        self.table_name = table_name
        self.schema = schema
        self.bplus_tree = bplus_tree
        self.key = key
        self.txn_id = txn_id
        self.lock_manager = lock_manager
        self.is_mvcc = is_mvcc
        
        # MVCC read snapshot elements
        self.active_tx_ids = read_snapshot[1] if read_snapshot else set()
        self.read_tx_id = read_snapshot[0] if read_snapshot else (txn_id if txn_id else 0)
        self.committed_txs = committed_txs if committed_txs else set()
        
        self.has_run = False

    def init(self):
        self.has_run = False

    def next(self):
        if self.has_run:
            return None
        self.has_run = True
        
        rid = self.bplus_tree.search(self.key)
        if rid is None:
            return None
            
        page_id, slot_idx = rid
        page = self.bpm.fetch_page(page_id)
        record = page.get_record(slot_idx)
        self.bpm.unpin_page(page_id, is_dirty=False)
        
        if record is None:
            return None
            
        xmin, xmax, payload = record
        
        # MVCC Visibility
        visible = is_visible(xmin, xmax, self.read_tx_id, self.active_tx_ids, self.committed_txs, self.is_mvcc)
        if not visible:
            return None
            
        # SS2PL Lock
        if not self.is_mvcc and self.txn_id is not None and self.lock_manager is not None:
            success = self.lock_manager.acquire_shared(self.txn_id, rid)
            if not success:
                raise TransactionAbortException(f"Txn {self.txn_id} aborted due to deadlock/lock timeout during IndexScan")
                
        values = self.schema.unpack(payload)
        row = {}
        for col, val in zip(self.schema.columns, values):
            row[col] = val
            row[f"{self.table_name}.{col}"] = val
        row["_rid"] = rid
        return row

    def close(self):
        pass


class FilterOperator(Operator):
    def __init__(self, child, condition):
        # condition: (col_name, op, value)
        self.child = child
        self.col, self.op, self.val = condition

    def init(self):
        self.child.init()

    def next(self):
        while True:
            row = self.child.next()
            if row is None:
                return None
                
            # Evaluate condition
            if self.col not in row:
                continue
                
            row_val = row[self.col]
            match = False
            if self.op == '=':
                match = (row_val == self.val)
            elif self.op == '>':
                match = (row_val > self.val)
            elif self.op == '<':
                match = (row_val < self.val)
            elif self.op == '>=':
                match = (row_val >= self.val)
            elif self.op == '<=':
                match = (row_val <= self.val)
            elif self.op == '!=':
                match = (row_val != self.val)
                
            if match:
                return row

    def close(self):
        self.child.close()


class ProjectOperator(Operator):
    def __init__(self, child, target_cols):
        self.child = child
        self.target_cols = target_cols

    def init(self):
        self.child.init()

    def next(self):
        row = self.child.next()
        if row is None:
            return None
            
        # Support SELECT * or specific columns
        if len(self.target_cols) == 1 and self.target_cols[0] == '*':
            return {k: v for k, v in row.items() if not k.startswith('_')}
            
        projected = {}
        for col in self.target_cols:
            if col in row:
                projected[col] = row[col]
            else:
                found = False
                for k in row.keys():
                    if k.endswith(f".{col}"):
                        projected[col] = row[k]
                        found = True
                        break
                if not found:
                    projected[col] = None
        return projected

    def close(self):
        self.child.close()


class NestedLoopJoinOperator(Operator):
    def __init__(self, outer, inner_creator, join_cond):
        # join_cond: (outer_col, inner_col)
        self.outer = outer
        self.inner_creator = inner_creator
        self.outer_col, self.inner_col = join_cond
        
        self.curr_outer_row = None
        self.curr_inner = None

    def init(self):
        self.outer.init()
        self.curr_outer_row = None
        if self.curr_inner:
            self.curr_inner.close()
        self.curr_inner = None

    def next(self):
        while True:
            if self.curr_outer_row is None:
                self.curr_outer_row = self.outer.next()
                if self.curr_outer_row is None:
                    return None  # End of outer table
                
                if self.curr_inner:
                    self.curr_inner.close()
                self.curr_inner = self.inner_creator(self.curr_outer_row)
                self.curr_inner.init()

            inner_row = self.curr_inner.next()
            if inner_row is None:
                self.curr_outer_row = None
                continue
                
            val_outer = self.curr_outer_row.get(self.outer_col)
            val_inner = inner_row.get(self.inner_col)
            
            if val_outer is None:
                for k in self.curr_outer_row.keys():
                    if k.endswith(f".{self.outer_col}"):
                        val_outer = self.curr_outer_row[k]
                        break
            if val_inner is None:
                for k in inner_row.keys():
                    if k.endswith(f".{self.inner_col}"):
                        val_inner = inner_row[k]
                        break

            if val_outer == val_inner and val_outer is not None:
                merged = {}
                for k, v in self.curr_outer_row.items():
                    if not k.startswith('_'):
                        merged[k] = v
                for k, v in inner_row.items():
                    if not k.startswith('_'):
                        merged[k] = v
                return merged

    def close(self):
        self.outer.close()
        if self.curr_inner:
            self.curr_inner.close()


class InsertOperator(Operator):
    def __init__(self, bpm, table_metadata, bplus_tree, values, txn_id=None, lock_manager=None, is_mvcc=False, wal_manager=None, tx_manager=None):
        self.bpm = bpm
        self.meta = table_metadata
        self.bplus_tree = bplus_tree
        self.values = values
        self.txn_id = txn_id if txn_id else 0
        self.lock_manager = lock_manager
        self.is_mvcc = is_mvcc
        self.wal_manager = wal_manager
        self.tx_manager = tx_manager

    def execute(self):
        payload = self.meta.schema.pack(self.values)
        
        pk_col = self.meta.primary_key
        pk_idx = self.meta.schema.columns.index(pk_col)
        pk_val = int(self.values[pk_idx])
        
        # 1. Check duplicate key
        if self.bplus_tree.search(pk_val) is not None:
            rid = self.bplus_tree.search(pk_val)
            page = self.bpm.fetch_page(rid[0])
            record = page.get_record(rid[1])
            self.bpm.unpin_page(rid[0], is_dirty=False)
            if record is not None:
                xmin, xmax, _ = record
                committed_txs = self.tx_manager.committed_txs if self.tx_manager else set()
                active_txs = self.tx_manager.active_txs if self.tx_manager else set()
                if is_visible(xmin, xmax, self.txn_id, active_txs, committed_txs, self.is_mvcc):
                    raise ValueError(f"Duplicate key error: primary key {pk_val} already exists.")

        # Fetch last page or create first page
        curr_page_id = self.meta.last_page_id
        if curr_page_id == 0xFFFFFFFF:
            page = self.bpm.new_page()
            curr_page_id = page.get_page_id()
            self.meta.first_page_id = curr_page_id
            self.meta.last_page_id = curr_page_id
            self.bpm.unpin_page(curr_page_id, is_dirty=True)

        page = self.bpm.fetch_page(curr_page_id)
        if not page.has_enough_space(len(payload) + 8):
            self.bpm.unpin_page(curr_page_id, is_dirty=False)
            new_page = self.bpm.new_page()
            new_page_id = new_page.get_page_id()
            
            old_page = self.bpm.fetch_page(curr_page_id)
            old_page.set_next_page_id(new_page_id)
            self.bpm.unpin_page(curr_page_id, is_dirty=True)
            
            self.meta.last_page_id = new_page_id
            page = new_page
            curr_page_id = new_page_id

        # Insert record into slotted page
        xmin = self.txn_id
        xmax = 0
        before_page = bytes(page.data)
        slot_idx = page.insert_record(xmin, xmax, payload)
        assert slot_idx != -1, "Insert failed"

        rid = (curr_page_id, slot_idx)
        after_page = bytes(page.data)

        # 2. Write WAL log record & Register update for Transaction Manager
        if self.wal_manager:
            lsn = self.wal_manager.log_update(self.txn_id, curr_page_id, before_page, after_page)
            page.set_lsn(lsn)

        # Register update for transaction rollback (undo)
        if self.tx_manager and self.txn_id != 0:
            self.tx_manager.register_update(self.txn_id, curr_page_id, before_page, after_page)

        # 3. 2PL Lock
        if not self.is_mvcc and self.txn_id != 0 and self.lock_manager is not None:
            success = self.lock_manager.acquire_exclusive(self.txn_id, rid)
            if not success:
                self.bpm.unpin_page(curr_page_id, is_dirty=False)
                raise TransactionAbortException(f"Txn {self.txn_id} aborted due to deadlock during Insert")

        self.bpm.unpin_page(curr_page_id, is_dirty=True)

        # 4. Insert into index
        self.bplus_tree.insert(pk_val, rid)
        return rid


class DeleteOperator(Operator):
    def __init__(self, bpm, table_metadata, bplus_tree, child, txn_id=None, lock_manager=None, is_mvcc=False, wal_manager=None, tx_manager=None):
        self.bpm = bpm
        self.meta = table_metadata
        self.bplus_tree = bplus_tree
        self.child = child
        self.txn_id = txn_id if txn_id else 0
        self.lock_manager = lock_manager
        self.is_mvcc = is_mvcc
        self.wal_manager = wal_manager
        self.tx_manager = tx_manager

    def execute(self):
        self.child.init()
        deleted_count = 0
        
        while True:
            row = self.child.next()
            if row is None:
                break
                
            rid = row["_rid"]
            pk_col = self.meta.primary_key
            pk_val = row[pk_col]
            
            page_id, slot_idx = rid
            page = self.bpm.fetch_page(page_id)
            
            # Write-Write Conflict Prevention:
            # 1. 2PL Mode: Acquire Exclusive lock on RID
            if not self.is_mvcc and self.txn_id != 0 and self.lock_manager is not None:
                success = self.lock_manager.acquire_exclusive(self.txn_id, rid)
                if not success:
                    self.bpm.unpin_page(page_id, is_dirty=False)
                    raise TransactionAbortException(f"Txn {self.txn_id} aborted due to deadlock during Delete")
                    
            # 2. MVCC Mode: Acquire Exclusive lock on RID to prevent concurrent updates
            if self.is_mvcc and self.txn_id != 0 and self.lock_manager is not None:
                success = self.lock_manager.acquire_exclusive(self.txn_id, rid)
                if not success:
                    self.bpm.unpin_page(page_id, is_dirty=False)
                    raise TransactionAbortException(f"Txn {self.txn_id} aborted due to Write-Write conflict or deadlock during Delete")

            # Re-verify visibility
            record = page.get_record(slot_idx)
            if record is None:
                self.bpm.unpin_page(page_id, is_dirty=False)
                continue
                
            xmin, xmax, payload = record
            if xmax != 0:
                if self.is_mvcc and xmax != self.txn_id:
                    self.bpm.unpin_page(page_id, is_dirty=False)
                    raise TransactionAbortException(f"Txn {self.txn_id} aborted due to Write-Write conflict on record delete")
                self.bpm.unpin_page(page_id, is_dirty=False)
                continue

            # Log full-page before/after images for WAL recovery
            before_page = bytes(page.data)

            # Perform deletion
            if self.is_mvcc:
                # Soft delete
                page.delete_record(slot_idx, xmax=self.txn_id)
            else:
                # Hard delete
                page.delete_record(slot_idx, xmax=0)
                self.bplus_tree.delete(pk_val)

            after_page = bytes(page.data)

            if self.wal_manager:
                lsn = self.wal_manager.log_update(self.txn_id, page_id, before_page, after_page)
                page.set_lsn(lsn)

            # Register update for Transaction Manager rollback (undo)
            if self.tx_manager and self.txn_id != 0:
                self.tx_manager.register_update(self.txn_id, page_id, before_page, after_page)

            self.bpm.unpin_page(page_id, is_dirty=True)
            deleted_count += 1
            
        return deleted_count
