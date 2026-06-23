import os
import json
from src.storage.disk_manager import DiskManager
from src.storage.buffer_pool import BufferPoolManager
from src.storage.schema import Schema
from src.index.b_plus_tree import BPlusTree
from src.query.parser import SQLParser
from src.query.operators import (
    SeqScanOperator, IndexScanOperator, FilterOperator, 
    ProjectOperator, NestedLoopJoinOperator, InsertOperator, DeleteOperator
)
from src.query.optimizer import Optimizer
from src.concurrency.lock_manager import LockManager
from src.concurrency.tx_manager import TransactionManager
from src.recovery.wal_manager import WALManager
from src.recovery.recovery_manager import RecoveryManager

class TableMetadata:
    def __init__(self, name, columns, types, primary_key, b_tree_root=0, first_page_id=0xFFFFFFFF, last_page_id=0xFFFFFFFF):
        self.name = name
        self.columns = columns
        self.types = types
        self.primary_key = primary_key
        self.b_tree_root = b_tree_root
        self.first_page_id = first_page_id
        self.last_page_id = last_page_id
        self.schema = Schema(columns, types)

    def to_dict(self):
        return {
            "name": self.name,
            "columns": self.columns,
            "types": self.types,
            "primary_key": self.primary_key,
            "b_tree_root": self.b_tree_root,
            "first_page_id": self.first_page_id,
            "last_page_id": self.last_page_id
        }

    @staticmethod
    def from_dict(d):
        return TableMetadata(
            d["name"], d["columns"], d["types"], d["primary_key"],
            d["b_tree_root"], d["first_page_id"], d["last_page_id"]
        )


class MiniDB:
    def __init__(self, db_dir, is_mvcc=False, pool_size=10):
        self.db_dir = db_dir
        self.is_mvcc = is_mvcc
        
        if not os.path.exists(db_dir):
            os.makedirs(db_dir)

        self.db_filepath = os.path.join(db_dir, "minidb.db")
        self.log_filepath = os.path.join(db_dir, "minidb.log")
        self.catalog_filepath = os.path.join(db_dir, "minidb.catalog")

        # Initialize core engine layers
        self.disk_manager = DiskManager(self.db_filepath)
        self.wal_manager = WALManager(self.log_filepath)
        self.bpm = BufferPoolManager(self.disk_manager, pool_size=pool_size, wal_manager=self.wal_manager)
        self.lock_manager = LockManager()
        self.tx_manager = TransactionManager(self.bpm, self.lock_manager, self.wal_manager, is_mvcc=is_mvcc)
        
        # Load tables catalog
        self.tables = {}
        self.b_plus_trees = {}
        self._load_catalog()

        # Initialize cost-based optimizer
        self.optimizer = Optimizer()
        # Only compute stats if no WAL recovery is pending (log file empty or non-existent)
        # Otherwise stats will be computed after recover() is called
        if not self._needs_recovery():
            self._update_optimizer_statistics()

    def _needs_recovery(self):
        if os.path.exists(self.log_filepath):
            return os.path.getsize(self.log_filepath) > 0
        return False

    def _load_catalog(self):
        if os.path.exists(self.catalog_filepath):
            with open(self.catalog_filepath, "r") as f:
                data = json.load(f)
                for name, d in data.items():
                    meta = TableMetadata.from_dict(d)
                    self.tables[name] = meta
                    self.b_plus_trees[name] = BPlusTree(self.bpm, root_page_id=meta.b_tree_root)

    def save_catalog(self):
        data = {name: meta.to_dict() for name, meta in self.tables.items()}
        with open(self.catalog_filepath, "w") as f:
            json.dump(data, f, indent=4)

    def _update_optimizer_statistics(self):
        # Scan tables and build stats
        for table_name, meta in self.tables.items():
            # For simplicity, count pages and rows using SeqScan in a background check
            pages_count = 0
            rows_count = 0
            
            curr_pid = meta.first_page_id
            visited = set()
            while curr_pid != 0xFFFFFFFF and curr_pid not in visited:
                visited.add(curr_pid)
                pages_count += 1
                page = self.bpm.fetch_page(curr_pid)
                # Count only active slots
                num_slots = page.get_num_slots()
                for i in range(num_slots):
                    rec = page.get_record(i)
                    if rec is not None:
                        rows_count += 1
                curr_pid = page.get_next_page_id()
                self.bpm.unpin_page(page.get_page_id(), is_dirty=False)
            
            # Setup simple column stats (e.g. min, max, distinct)
            col_stats = {}
            for col in meta.columns:
                col_stats[col] = {
                    "min_val": 1,
                    "max_val": rows_count + 10,
                    "num_distinct": max(1, rows_count)
                }
                
            self.optimizer.set_stats(table_name, max(1, rows_count), max(1, pages_count), col_stats)

    def recover(self):
        # Runs crash recovery
        recovery_mgr = RecoveryManager(self.log_filepath, self.bpm)
        recovery_mgr.run_recovery(self.wal_manager)
        
        # Reload catalog as recovery might have modified pages
        self._load_catalog()
        self._update_optimizer_statistics()

    def checkpoint(self):
        # Flush all dirty pages to disk
        self.bpm.flush_all()
        # Flush WAL log
        if self.wal_manager:
            self.wal_manager.flush_to_lsn(self.wal_manager.next_lsn)

    def close(self):
        self.checkpoint()
        self.bpm.flush_all()
        self.disk_manager.close()
        self.wal_manager.close()

    def execute_sql(self, sql_str, txn_ctx=None):
        parsed = SQLParser.parse(sql_str)
        stmt_type = parsed["type"]

        if stmt_type == "CREATE":
            return self._execute_create(parsed)

        # For writing / reading queries, handle Transaction Context
        # If autocommit (txn_ctx is None):
        is_autocommit = (txn_ctx is None)
        if is_autocommit:
            txn_ctx = self.tx_manager.begin()

        try:
            res = None
            if stmt_type == "INSERT":
                res = self._execute_insert(parsed, txn_ctx)
            elif stmt_type == "DELETE":
                res = self._execute_delete(parsed, txn_ctx)
            elif stmt_type == "SELECT":
                res = self._execute_select(parsed, txn_ctx)

            if is_autocommit:
                self.tx_manager.commit(txn_ctx.tx_id)
            return res

        except Exception as e:
            if is_autocommit:
                self.tx_manager.abort(txn_ctx.tx_id)
            raise e

    def _execute_create(self, parsed):
        name = parsed["table_name"]
        columns = parsed["columns"]
        types = parsed["types"]
        pk = parsed["primary_key"]

        if name in self.tables:
            raise ValueError(f"Table {name} already exists.")

        if pk not in columns:
            raise ValueError(f"Primary key {pk} must be one of the columns.")

        # Allocate root page for B+ Tree
        b_tree = BPlusTree(self.bpm, root_page_id=None)
        
        # Allocate first page of heap file
        first_page = self.bpm.new_page()
        first_page_id = first_page.get_page_id()
        self.bpm.unpin_page(first_page_id, is_dirty=True)

        meta = TableMetadata(
            name, columns, types, pk,
            b_tree_root=b_tree.root_page_id,
            first_page_id=first_page_id,
            last_page_id=first_page_id
        )

        self.tables[name] = meta
        self.b_plus_trees[name] = b_tree
        self.save_catalog()
        self._update_optimizer_statistics()
        return f"Table {name} created successfully."

    def _execute_insert(self, parsed, txn_ctx):
        name = parsed["table_name"]
        values = parsed["values"]

        if name not in self.tables:
            raise ValueError(f"Table {name} does not exist.")

        meta = self.tables[name]
        b_tree = self.b_plus_trees[name]

        # Use InsertOperator
        op = InsertOperator(
            self.bpm, meta, b_tree, values,
            txn_id=txn_ctx.tx_id,
            lock_manager=self.lock_manager,
            is_mvcc=self.is_mvcc,
            wal_manager=self.wal_manager,
            tx_manager=self.tx_manager
        )
        rid = op.execute()
        self._update_optimizer_statistics()
        return f"1 row inserted at RecordID {rid}."

    def _execute_delete(self, parsed, txn_ctx):
        name = parsed["table_name"]
        where = parsed["where"]

        if name not in self.tables:
            raise ValueError(f"Table {name} does not exist.")

        meta = self.tables[name]
        b_tree = self.b_plus_trees[name]

        # Scan child for records to delete
        # Use optimizer to choose best scan
        scan_op = self._get_access_path(name, where, txn_ctx)
        
        op = scan_op
        if where:
            col, op_symbol, val = where
            is_index_scan = isinstance(scan_op, IndexScanOperator)
            if not (is_index_scan and col == meta.primary_key and op_symbol == '='):
                op = FilterOperator(op, where)
        
        # Wrap with delete operator
        del_op = DeleteOperator(
            self.bpm, meta, b_tree, op,
            txn_id=txn_ctx.tx_id,
            lock_manager=self.lock_manager,
            is_mvcc=self.is_mvcc,
            wal_manager=self.wal_manager,
            tx_manager=self.tx_manager
        )
        count = del_op.execute()
        self._update_optimizer_statistics()
        return f"{count} rows deleted."

    def _execute_select(self, parsed, txn_ctx):
        cols = parsed["columns"]
        table_name = parsed["table_name"]
        join_table = parsed["join_table"]
        join_cond = parsed["join_condition"]
        where = parsed["where"]

        if table_name not in self.tables:
            raise ValueError(f"Table {table_name} does not exist.")

        # Resolve read snapshot and committed txs for MVCC
        snapshot = txn_ctx.snapshot if txn_ctx else None
        committed_txs = self.tx_manager.committed_txs

        # 1. Single Table Query
        if not join_table:
            scan_op = self._get_access_path(table_name, where, txn_ctx)
            
            # Apply Filter if not already handled by IndexScan
            op = scan_op
            if where:
                col, op_symbol, val = where
                # If IndexScan is used, it already filtered point PK.
                # If SeqScan, or IndexScan on a different condition, wrap in FilterOperator.
                is_index_scan = isinstance(scan_op, IndexScanOperator)
                if not (is_index_scan and col == self.tables[table_name].primary_key and op_symbol == '='):
                    op = FilterOperator(op, where)
                    
            # Apply Projection
            op = ProjectOperator(op, cols)
            
            # Retrieve rows
            rows = []
            op.init()
            while True:
                row = op.next()
                if row is None:
                    break
                rows.append(row)
            op.close()
            return rows

        # 2. Join Query
        else:
            if join_table not in self.tables:
                raise ValueError(f"Join table {join_table} does not exist.")
                
            pk_a = self.tables[table_name].primary_key
            pk_b = self.tables[join_table].primary_key
            
            # Run Join Order Optimization
            # Join condition: (col_a, col_b)
            col_a, col_b = join_cond
            
            b_tree_a_height = 2  # simple default
            b_tree_b_height = 2
            
            plan = self.optimizer.choose_join_order(
                table_name, join_table, join_cond,
                pk_a, pk_b, b_tree_a_height, b_tree_b_height
            )
            
            outer_table, inner_table = plan["order"]
            print(f"[Optimizer] Selected join order: {outer_table} JOIN {inner_table} (Estimated Cost: {plan['cost']:.1f} vs {max(plan['costs']):.1f})")
            
            # Build join operator tree
            # Outer scan is always SeqScan
            outer_meta = self.tables[outer_table]
            outer_op = SeqScanOperator(
                self.bpm, outer_table, outer_meta.schema, outer_meta.first_page_id,
                txn_id=txn_ctx.tx_id, lock_manager=self.lock_manager,
                is_mvcc=self.is_mvcc, read_snapshot=snapshot, committed_txs=committed_txs
            )
            
            # Inner creator: if inner can use index (i.e. join column is inner's PK)
            inner_meta = self.tables[inner_table]
            inner_join_col = col_b if inner_table == join_table else col_a
            inner_join_col_clean = inner_join_col.split('.')[-1]
            
            inner_pk = inner_meta.primary_key
            
            if inner_join_col_clean == inner_pk:
                # Use Index Join Creator!
                print(f"[Optimizer] Inner relation {inner_table} will scan via B+ Tree Index.")
                def inner_creator(outer_row):
                    # Get join key value from outer row
                    outer_join_col = col_a if outer_table == table_name else col_b
                    key_val = outer_row.get(outer_join_col)
                    if key_val is None:
                        # Try resolution without prefix
                        key_val = outer_row.get(outer_join_col.split('.')[-1])
                    return IndexScanOperator(
                        self.bpm, inner_table, inner_meta.schema, self.b_plus_trees[inner_table],
                        key_val, txn_id=txn_ctx.tx_id, lock_manager=self.lock_manager,
                        is_mvcc=self.is_mvcc, read_snapshot=snapshot, committed_txs=committed_txs
                    )
            else:
                # Fallback to SeqScan Inner Nested Loop Join
                print(f"[Optimizer] Inner relation {inner_table} will scan via Table SeqScan.")
                def inner_creator(outer_row):
                    return SeqScanOperator(
                        self.bpm, inner_table, inner_meta.schema, inner_meta.first_page_id,
                        txn_id=txn_ctx.tx_id, lock_manager=self.lock_manager,
                        is_mvcc=self.is_mvcc, read_snapshot=snapshot, committed_txs=committed_txs
                    )

            # Nested Loop Join Operator
            # Swap join columns if optimizer reversed the table order
            if outer_table == table_name:
                join_cols = (col_a, col_b)
            else:
                join_cols = (col_b, col_a)
            join_op = NestedLoopJoinOperator(outer_op, inner_creator, join_cols)
            
            # Wrap in filter if where exists
            op = join_op
            if where:
                op = FilterOperator(op, where)
                
            # Project output columns
            op = ProjectOperator(op, cols)
            
            rows = []
            op.init()
            while True:
                row = op.next()
                if row is None:
                    break
                rows.append(row)
            op.close()
            return rows

    def _get_access_path(self, table_name, condition, txn_ctx):
        meta = self.tables[table_name]
        pk_col = meta.primary_key
        b_tree = self.b_plus_trees[table_name]
        
        # Call access path optimizer
        scan_type, chosen_cost, other_cost = self.optimizer.choose_access_path(
            table_name, condition, pk_col, b_tree_height=2
        )
        
        snapshot = txn_ctx.snapshot if txn_ctx else None
        committed_txs = self.tx_manager.committed_txs
        
        if scan_type == "IndexScan":
            key_val = int(condition[2])
            print(f"[Optimizer] Selected IndexScan for {table_name} on PK={key_val} (Cost: {chosen_cost:.1f} vs SeqScan: {other_cost:.1f})")
            return IndexScanOperator(
                self.bpm, table_name, meta.schema, b_tree, key_val,
                txn_id=txn_ctx.tx_id, lock_manager=self.lock_manager,
                is_mvcc=self.is_mvcc, read_snapshot=snapshot, committed_txs=committed_txs
            )
        else:
            print(f"[Optimizer] Selected SeqScan for {table_name} (Cost: {chosen_cost:.1f} vs IndexScan: {other_cost:.1f})")
            return SeqScanOperator(
                self.bpm, table_name, meta.schema, meta.first_page_id,
                txn_id=txn_ctx.tx_id, lock_manager=self.lock_manager,
                is_mvcc=self.is_mvcc, read_snapshot=snapshot, committed_txs=committed_txs
            )
