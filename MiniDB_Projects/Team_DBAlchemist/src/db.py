"""
MiniDB — top-level database engine.

Wires together:
  storage (HeapFile + BufferPool)
  indexing (BPlusTree)
  catalog (schema)
  transactions (TransactionManager)
  MVCC
  WAL + crash recovery
  SQL executor + optimizer

Usage:
  db = MiniDB('data/')
  db.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)")
  txn = db.begin()
  db.execute("INSERT INTO users (id, name, age) VALUES (1, 'Alice', 30)", txn)
  db.commit(txn)
  rows = db.execute("SELECT * FROM users WHERE age > 25")
"""
import os
import json

from storage.heap_file import HeapFile
from storage.buffer_pool import BufferPool
from index.bplus_tree import BPlusTree
from catalog.catalog import Catalog, Column
from txn.transaction import TransactionManager, Transaction
from txn.mvcc import MVCCManager
from recovery.wal import WAL
from optimizer.optimizer import Optimizer, TableStats
from sql.executor import Executor, serialize_row, deserialize_row
from sql.planner import Planner
from sql.parser import CreateTableStmt


class MiniDB:
    def __init__(self, data_dir: str, buffer_capacity: int = 128):
        os.makedirs(data_dir, exist_ok=True)
        self.data_dir = data_dir
        self.buffer_capacity = buffer_capacity

        # catalog
        self.catalog = Catalog(os.path.join(data_dir, 'catalog.json'))

        # transaction + MVCC
        self.txn_manager = TransactionManager()
        self.mvcc = MVCCManager(self.txn_manager)

        # WAL
        self.wal = WAL(os.path.join(data_dir, 'wal.log'))

        # optimizer
        self.optimizer = Optimizer()

        # table registry: {table_name: {bp, schema, tree, pk_col, index_col}}
        self.registry: dict = {}

        # load existing tables
        for table_name in self.catalog.list_tables():
            self._load_table(table_name)

        # crash recovery
        self._recover()

        # planner + executor
        self.executor = Executor(self.registry, self.mvcc, self.wal, self.optimizer)
        self.planner = Planner(self)

    # ── public API ────────────────────────────────────────────────────────────

    def execute(self, sql: str, txn: Transaction = None):
        """Execute SQL string. Returns list of result dicts."""
        result, _ = self.planner.plan_and_execute(sql, txn)
        return result

    def begin(self, read_only: bool = False) -> Transaction:
        txn = self.txn_manager.begin(read_only=read_only)
        if not read_only:
            self.wal.log_begin(txn.txid)
        return txn

    def commit(self, txn: Transaction):
        self.wal.log_commit(txn.txid)
        self.txn_manager.commit(txn)
        self._flush_all()
        self._save_indexes()

    def rollback(self, txn: Transaction):
        self._apply_undo(txn)
        if not txn.read_only:
            self.wal.log_abort(txn.txid)
        self.txn_manager.abort(txn)
        if not txn.read_only:
            self._flush_all()

    def close(self):
        self._flush_all()
        self._save_indexes()
        self.wal.close()

    # ── DDL ───────────────────────────────────────────────────────────────────

    def create_table(self, stmt: CreateTableStmt):
        cols = [Column(c['name'], c['type'], c.get('primary_key', False))
                for c in stmt.col_defs]
        schema = self.catalog.create_table(stmt.table, cols)
        self._load_table(stmt.table)
        self._update_stats(stmt.table)
        return [{'created': stmt.table}]

    def drop_table(self, table: str):
        self.catalog.drop_table(table)
        if table in self.registry:
            self.registry[table]['bp'].flush_all()
            del self.registry[table]
        return [{'dropped': table}]

    def evict_buffer_cache(self, table: str = None):
        """Evict all unpinned pages from buffer pool(s). Used to simulate cold reads in benchmarks."""
        tables = [table] if table else list(self.registry.keys())
        for t in tables:
            bp = self.registry[t]['bp']
            bp.flush_all()
            # evict by resetting pool structures
            bp._frames.clear()
            bp._clock_hand.clear()
            bp._hand_pos = 0

    # ── internals ─────────────────────────────────────────────────────────────

    def _load_table(self, table_name: str):
        schema = self.catalog.get_table(table_name)
        if schema is None:
            return

        heap_path = os.path.join(self.data_dir, f'{table_name}.heap')
        heap = HeapFile(heap_path)
        bp = BufferPool(heap, capacity=self.buffer_capacity)

        pk_col = schema.primary_key_col()
        tree = None
        index_col = None

        if pk_col:
            tree = BPlusTree(name=f'{table_name}_pk')
            idx_path = os.path.join(self.data_dir, f'{table_name}.idx')
            tree.load(idx_path)
            index_col = pk_col.name

        self.registry[table_name] = {
            'bp': bp,
            'schema': schema,
            'tree': tree,
            'pk_col': pk_col.name if pk_col else None,
            'index_col': index_col,
        }

    def _save_indexes(self):
        for table_name, entry in self.registry.items():
            if entry.get('tree'):
                idx_path = os.path.join(self.data_dir, f'{table_name}.idx')
                entry['tree'].save(idx_path)

    def _flush_all(self):
        for entry in self.registry.values():
            entry['bp'].flush_all()

    def _update_stats(self, table_name: str):
        entry = self.registry.get(table_name)
        if not entry:
            return
        bp = entry['bp']
        stats = TableStats(num_rows=0, num_pages=max(1, bp.heap.num_pages))
        self.optimizer.update_stats(table_name, stats)

    def _recover(self):
        """Replay WAL for committed transactions after crash."""
        redo_records, committed_xids = self.wal.recover()
        if not redo_records:
            return

        print(f"[WAL] Recovering {len(redo_records)} records for {len(committed_xids)} committed txns...")

        next_xid = max(committed_xids, default=0) + 1
        self.txn_manager.restore_state(committed_xids, next_xid)

        for rec in redo_records:
            table = rec.get('table')
            entry = self.registry.get(table)
            if not entry:
                continue

            op = rec['op']
            row = rec['row']
            page_id = rec['page_id']
            slot_id = rec['slot_id']
            bp: BufferPool = entry['bp']
            tree = entry.get('tree')
            pk_col = entry.get('pk_col')

            if op == 'INSERT':
                page = bp.fetch_page(page_id)
                existing = page.get_record(slot_id)
                if existing is None:
                    # page/slot doesn't exist yet — insert
                    rec_bytes = serialize_row(row)
                    page.insert_record(rec_bytes)
                    if tree and pk_col:
                        data = {k: v for k, v in row.items() if not k.startswith('_')}
                        if pk_col in data:
                            tree.insert(data[pk_col], (page_id, slot_id))
                bp.unpin_page(page_id, dirty=True)

            elif op == 'DELETE':
                page = bp.fetch_page(page_id)
                rec_bytes = serialize_row(row)
                page.update_record(slot_id, rec_bytes)
                bp.unpin_page(page_id, dirty=True)

        self._flush_all()
        self._save_indexes()
        print(f"[WAL] Recovery complete.")

    def _apply_undo(self, txn: Transaction):
        """Rollback: undo all writes by this transaction in reverse order."""
        for undo in txn.undo_log():
            table = undo.get('table')
            entry = self.registry.get(table)
            if not entry:
                continue

            bp: BufferPool = entry['bp']
            op = undo['op']
            page_id = undo['page_id']
            slot_id = undo['slot_id']

            if op == 'undo_insert':
                # remove the inserted row
                page = bp.fetch_page(page_id)
                page.delete_record(slot_id)
                bp.unpin_page(page_id, dirty=True)

            elif op == 'undo_delete':
                # restore original row (remove _xmax)
                original_row = undo['row']
                restored = {**original_row, '_xmax': None}
                rec = serialize_row(restored)
                page = bp.fetch_page(page_id)
                page.update_record(slot_id, rec)
                bp.unpin_page(page_id, dirty=True)

    # ── convenience stats ─────────────────────────────────────────────────────

    def refresh_stats(self, table_name: str):
        """Recompute table statistics for optimizer."""
        entry = self.registry.get(table_name)
        if not entry:
            return
        bp = entry['bp']
        schema = entry['schema']

        # create a dummy txn to scan
        txn = self.begin()
        from sql.executor import SeqScan
        from optimizer.optimizer import TableStats
        scanner = SeqScan(bp, table_name, schema, txn, self.mvcc)
        col_values: dict = {c.name: [] for c in schema.columns}
        row_count = 0
        for row, _, _ in scanner.scan():
            row_count += 1
            clean = {k: v for k, v in row.items() if not k.startswith('_')}
            for col, val in clean.items():
                if col in col_values:
                    col_values[col].append(val)
        self.rollback(txn)

        stats = TableStats(num_rows=row_count, num_pages=max(1, bp.heap.num_pages))
        for col, vals in col_values.items():
            stats.update(col, len(set(vals)))
        self.optimizer.update_stats(table_name, stats)
