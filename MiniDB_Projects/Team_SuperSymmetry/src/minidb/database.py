"""
MiniDB database engine — the integration layer.

This module ties every component together behind a single `Database` object
with one public entry point, `execute(sql)`. It owns:

    DiskManager  + BufferPool      (storage)
    WALManager                     (durability / recovery)
    LockManager                    (strict 2PL, serializable)
    MVCCStore (per table) + Clock  (extension track B)
    Catalog + Optimizer + executor (query processing)

Two isolation modes share the same SQL front-end and the same physical
operators; only the *execution context* differs:

    * "2PL"  : reads take shared locks, writes take exclusive locks, all held
               to commit (strict two-phase locking). Durable via WAL; the
               recovery path is exercised by this mode.
    * "MVCC" : readers take no locks and see a consistent snapshot as of the
               transaction's start timestamp; writers create new versions and
               conflicting writers abort (first committer wins).

Autocommit wraps any statement issued outside an explicit BEGIN in its own
transaction.
"""
from __future__ import annotations

import os
import pickle
import threading
from typing import Any, Dict, Iterator, List, Optional, Tuple

from . import sql as ast
from .buffer_pool import BufferPool
from .catalog import Catalog, ColumnStats, TableInfo
from .disk_manager import DiskManager
from .executor import CompiledPredicate
from .heap_file import HeapFile
from .lock_manager import DeadlockError, LockManager, LockMode
from .mvcc import Clock, MVCCConflict, MVCCStore
from .optimizer import Optimizer
from .page import Page
from .sql import Parser
from .transaction import ABORTED, ACTIVE, COMMITTED, Transaction
from .types import Column, DataType, Schema
from .wal import ABORT, BEGIN, COMMIT, UPDATE, WALManager

_TYPE_MAP = {
    "INT": DataType.INT,
    "INTEGER": DataType.INT,
    "FLOAT": DataType.FLOAT,
    "REAL": DataType.FLOAT,
    "TEXT": DataType.TEXT,
    "VARCHAR": DataType.TEXT,
    "STRING": DataType.TEXT,
    "BOOL": DataType.BOOL,
    "BOOLEAN": DataType.BOOL,
}


class TransactionError(Exception):
    pass


class Result:
    """A SELECT result set: ordered column labels plus row dicts."""

    def __init__(self, columns: List[str], rows: List[Dict[str, Any]]):
        self.columns = columns
        self.rows = rows

    def __iter__(self):
        return iter(self.rows)

    def __len__(self):
        return len(self.rows)

    def __repr__(self):
        if not self.columns:
            return "(no rows)"
        widths = {c: len(str(c)) for c in self.columns}
        for r in self.rows:
            for c in self.columns:
                widths[c] = max(widths[c], len(_fmt(r.get(c))))
        head = " | ".join(str(c).ljust(widths[c]) for c in self.columns)
        sep = "-+-".join("-" * widths[c] for c in self.columns)
        body = "\n".join(
            " | ".join(_fmt(r.get(c)).ljust(widths[c]) for c in self.columns)
            for r in self.rows
        )
        n = len(self.rows)
        return f"{head}\n{sep}\n{body}\n({n} row{'s' if n != 1 else ''})"


def _fmt(v: Any) -> str:
    if v is None:
        return "NULL"
    if isinstance(v, float):
        return f"{v:g}"
    return str(v)


# ---------------------------------------------------------------------------
# Execution contexts
# ---------------------------------------------------------------------------
class _BaseCtx:
    """Shared row-decoding logic for both isolation modes."""

    def __init__(self, db: "Database", txn: Transaction):
        self.db = db
        self.txn = txn

    def row_dict(self, table: str, schema: Schema, raw: bytes) -> Dict[str, Any]:
        values = schema.deserialize(raw)
        real = self.db.catalog.get(table)
        return {f"{table}.{c.name}": v
                for c, v in zip(real.schema.columns, values)}


class TwoPLCtx(_BaseCtx):
    """Strict two-phase locking context: lock rows as they are touched."""

    def _slock(self, table: str, rid):
        self.db.lock_mgr.acquire(self.txn.txn_id, (table, rid), LockMode.S)

    def scan_table(self, table: str) -> Iterator[Tuple[Any, bytes]]:
        heap = self.db.heaps[table]
        for rid, raw in heap.scan():
            self._slock(table, rid)
            yield rid, raw

    def get_row(self, table: str, rid) -> Optional[bytes]:
        self._slock(table, rid)
        return self.db.heaps[table].get(rid)

    def index_lookup(self, table: str, column: str, low, high):
        info = self.db.catalog.get(table)
        tree = info.indexes[column]
        if low is not None and low == high:
            return list(tree.search(low))
        return list(tree.range_scan(low, high))


class MVCCCtx(_BaseCtx):
    """Snapshot-isolation context: lock-free reads from the version store."""

    def scan_table(self, table: str) -> Iterator[Tuple[Any, bytes]]:
        store = self.db.mvcc[table]
        for rid, raw in store.scan(self.txn):
            yield rid, raw

    def get_row(self, table: str, rid) -> Optional[bytes]:
        return self.db.mvcc[table].get(rid, self.txn)

    def index_lookup(self, table: str, column: str, low, high):
        # index maps key -> mvcc rid; filter by snapshot visibility downstream
        info = self.db.catalog.get(table)
        tree = info.indexes[column]
        if low is not None and low == high:
            rids = list(tree.search(low))
        else:
            rids = list(tree.range_scan(low, high))
        return rids


# ---------------------------------------------------------------------------
# Database
# ---------------------------------------------------------------------------
class Database:
    def __init__(self, directory: str, isolation: str = "2PL",
                 buffer_capacity: int = 256):
        self.dir = directory
        os.makedirs(directory, exist_ok=True)
        self.isolation = isolation.upper()
        if self.isolation not in ("2PL", "MVCC"):
            raise ValueError("isolation must be '2PL' or 'MVCC'")

        self.wal = WALManager(directory)
        self.disk = DiskManager(directory)
        self.bufferpool = BufferPool(
            self.disk, capacity=buffer_capacity, log_flush=self.wal.flush
        )
        self.lock_mgr = LockManager()
        self.catalog = Catalog()
        self.optimizer = Optimizer(self.catalog)
        self.parser = Parser
        self.clock = Clock()
        self.heaps: Dict[str, HeapFile] = {}
        self.mvcc: Dict[str, MVCCStore] = {}
        # mvcc rid -> heap rid, per table (for write-through durability)
        self._txn_counter = 0
        self._txn_lock = threading.RLock()      # physical latch for writes
        self._active: Dict[int, Transaction] = {}
        self._meta_path = os.path.join(directory, "meta.pkl")

        self._load_metadata()
        self._recover()
        self._rebuild_indexes_and_stats()
        if self.isolation == "MVCC":
            self._seed_mvcc()

    # ---- metadata persistence -------------------------------------------
    def _load_metadata(self):
        if not os.path.exists(self._meta_path):
            return
        with open(self._meta_path, "rb") as f:
            meta = pickle.load(f)
        for t in meta["tables"]:
            schema = Schema([Column(n, DataType[ty]) for n, ty in t["columns"]])
            info = TableInfo(
                name=t["name"], schema=schema,
                primary_key=t["primary_key"], file_key=t["file_key"],
            )
            self.catalog.add_table(info)
            self.heaps[t["name"]] = HeapFile(self.bufferpool, t["file_key"])
            for col in t["index_columns"]:
                from .btree import BPlusTree
                info.indexes[col] = BPlusTree()

    def _save_metadata(self):
        tables = []
        for name, info in self.catalog.tables.items():
            tables.append({
                "name": name,
                "columns": [(c.name, c.type.name) for c in info.schema.columns],
                "primary_key": info.primary_key,
                "file_key": info.file_key,
                "index_columns": list(info.indexes.keys()),
            })
        tmp = self._meta_path + ".tmp"
        with open(tmp, "wb") as f:
            pickle.dump({"tables": tables}, f)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, self._meta_path)

    # ---- recovery --------------------------------------------------------
    def _recover(self):
        if not self.heaps:
            return

        def heap_for(table):
            if table not in self.heaps:
                # table known only from log; create handle
                self.heaps[table] = HeapFile(self.bufferpool, table)
            return self.heaps[table]

        self.last_recovery = self.wal.recover(heap_for)
        self.bufferpool.flush_all()
        self.disk.fsync_all()

    def _rebuild_indexes_and_stats(self):
        """After recovery, rebuild B+ trees from heap contents and recompute
        statistics so the optimizer has fresh cardinalities."""
        for name, info in self.catalog.tables.items():
            heap = self.heaps[name]
            for col, tree in info.indexes.items():
                idx = info.schema.index_of(col)
                # clear and rebuild
                from .btree import BPlusTree
                fresh = BPlusTree()
                for rid, raw in heap.scan():
                    vals = info.schema.deserialize(raw)
                    fresh.insert(vals[idx], rid)
                info.indexes[col] = fresh
            self.catalog.analyze(name, heap, info.schema.deserialize)

    def _seed_mvcc(self):
        """Load persisted committed rows into per-table MVCC stores, and remap
        each table's indexes to the synthetic MVCC rids."""
        from .btree import BPlusTree
        for name, info in self.catalog.tables.items():
            store = MVCCStore(self.clock)
            self.mvcc[name] = store
            # rebuild indexes keyed on mvcc rids
            new_indexes = {c: BPlusTree() for c in info.indexes}
            for _heap_rid, raw in self.heaps[name].scan():
                mrid = store.seed(raw)
                vals = info.schema.deserialize(raw)
                for col, tree in new_indexes.items():
                    tree.insert(vals[info.schema.index_of(col)], mrid)
            info.indexes = new_indexes

    # ---- transaction lifecycle ------------------------------------------
    def _new_txn(self) -> Transaction:
        with self._txn_lock:
            self._txn_counter += 1
            tid = self._txn_counter
        read_ts = self.clock.now() if self.isolation == "MVCC" else None
        txn = Transaction(tid, self.isolation, read_ts=read_ts)
        self._active[tid] = txn
        # BEGIN is logged lazily on the first write so read-only transactions
        # touch neither the log nor the disk.
        txn.did_write = False
        txn.logged_begin = False
        return txn

    def _ensure_begin(self, txn: Transaction):
        if not txn.logged_begin:
            self.wal.append(kind=BEGIN, txn_id=txn.txn_id)
            txn.logged_begin = True
        txn.did_write = True

    def begin(self) -> Transaction:
        txn = self._new_txn()
        return txn

    def commit(self, txn: Transaction):
        if not txn.is_active():
            raise TransactionError("transaction not active")
        if self.isolation == "MVCC":
            if txn.did_write:
                for store in self.mvcc.values():
                    store.commit(txn)
        if txn.did_write:
            self.wal.append(kind=COMMIT, txn_id=txn.txn_id)
            self.wal.flush()
            self.bufferpool.flush_all()
            self.disk.fsync_all()
        if self.isolation == "2PL":
            self.lock_mgr.release_all(txn.txn_id)
        txn.state = COMMITTED
        self._active.pop(txn.txn_id, None)

    def abort(self, txn: Transaction):
        if not txn.is_active():
            return
        if not txn.did_write:
            # read-only: nothing logged, nothing to undo
            if self.isolation == "2PL":
                self.lock_mgr.release_all(txn.txn_id)
            txn.state = ABORTED
            self._active.pop(txn.txn_id, None)
            return
        if self.isolation == "2PL":
            # undo this txn's heap changes by applying before-images in
            # reverse, each as a fresh *logged* compensating record (CLR) so
            # the rollback is itself durable and recoverable.
            mine = [r for r in self.wal.records
                    if r.kind == UPDATE and r.txn_id == txn.txn_id]
            for r in reversed(mine):
                new_lsn = self.wal.append(kind=UPDATE, txn_id=txn.txn_id,
                                          table=r.table, rid=r.rid,
                                          before=r.after, after=r.before)
                self.heaps[r.table].redo_set(r.rid, r.before, new_lsn)
        # in-memory index maintenance (B+ trees live outside the heap/WAL)
        for fn in reversed(txn.undo):
            try:
                fn()
            except Exception:
                pass
        if self.isolation == "MVCC":
            for store in self.mvcc.values():
                store.abort(txn)
        self.wal.append(kind=ABORT, txn_id=txn.txn_id)
        self.wal.flush()
        if self.isolation == "2PL":
            self.bufferpool.flush_all()
            self.disk.fsync_all()
            self.lock_mgr.release_all(txn.txn_id)
        txn.state = ABORTED
        self._active.pop(txn.txn_id, None)

    # ---- public API ------------------------------------------------------
    def execute(self, sql: str, txn: Optional[Transaction] = None):
        """Execute a single SQL statement. If `txn` is None the statement runs
        in its own autocommit transaction."""
        node = Parser(sql).parse()

        # transaction-control statements
        if isinstance(node, ast.TxnControl):
            return self._exec_txn_control(node, txn)

        owns = txn is None
        if owns:
            txn = self._new_txn()
            txn.autocommit = True
        try:
            result = self._dispatch(node, txn)
            if owns:
                self.commit(txn)
            return result
        except (DeadlockError, MVCCConflict) as e:
            self.abort(txn)
            raise TransactionError(f"transaction aborted: {e}") from e
        except Exception:
            if owns and txn.is_active():
                self.abort(txn)
            raise

    def _exec_txn_control(self, node: ast.TxnControl, txn):
        action = node.action.upper()
        if action == "BEGIN":
            if txn is not None:
                raise TransactionError("already in a transaction")
            return self.begin()
        if action == "COMMIT":
            if txn is None:
                raise TransactionError("no active transaction")
            self.commit(txn)
            return None
        if action in ("ABORT", "ROLLBACK"):
            if txn is None:
                raise TransactionError("no active transaction")
            self.abort(txn)
            return None
        raise TransactionError(f"unknown txn control {action}")

    def _dispatch(self, node, txn: Transaction):
        if isinstance(node, ast.CreateTable):
            return self._create_table(node, txn)
        if isinstance(node, ast.CreateIndex):
            return self._create_index(node, txn)
        if isinstance(node, ast.Insert):
            return self._insert(node, txn)
        if isinstance(node, ast.Delete):
            return self._delete(node, txn)
        if isinstance(node, ast.Select):
            return self._select(node, txn)
        raise TransactionError(f"unsupported statement: {type(node).__name__}")

    # ---- DDL -------------------------------------------------------------
    def _create_table(self, node: ast.CreateTable, txn):
        if self.catalog.exists(node.name):
            raise TransactionError(f"table {node.name} already exists")
        columns = []
        for cname, ctype in node.columns:
            t = _TYPE_MAP.get(ctype.upper())
            if t is None:
                raise TransactionError(f"unknown type {ctype}")
            columns.append(Column(cname, t))
        schema = Schema(columns)
        file_key = node.name
        info = TableInfo(name=node.name, schema=schema,
                         primary_key=node.primary_key, file_key=file_key)
        self.catalog.add_table(info)
        self.heaps[node.name] = HeapFile(self.bufferpool, file_key)
        if self.isolation == "MVCC":
            self.mvcc[node.name] = MVCCStore(self.clock)
        # ensure the data file exists
        if self.disk.num_pages(file_key) == 0:
            pid, _ = self.bufferpool.new_page(file_key)
            self.bufferpool.unpin_page(pid, dirty=True)
        # primary key gets an index automatically
        if node.primary_key:
            from .btree import BPlusTree
            info.indexes[node.primary_key] = BPlusTree()
        self._save_metadata()
        return f"table {node.name} created"

    def _create_index(self, node: ast.CreateIndex, txn):
        info = self.catalog.get(node.table)
        if node.column in info.indexes:
            return f"index on {node.table}.{node.column} already exists"
        from .btree import BPlusTree
        tree = BPlusTree()
        idx = info.schema.index_of(node.column)
        if self.isolation == "MVCC":
            store = self.mvcc[node.table]
            for rid, raw in store.scan(txn):
                vals = info.schema.deserialize(raw)
                tree.insert(vals[idx], rid)
        else:
            for rid, raw in self.heaps[node.table].scan():
                vals = info.schema.deserialize(raw)
                tree.insert(vals[idx], rid)
        info.indexes[node.column] = tree
        self._save_metadata()
        return f"index created on {node.table}.{node.column}"

    # ---- DML -------------------------------------------------------------
    def _row_values(self, info: TableInfo, columns, raw_row):
        """Map a parsed INSERT row to a full, ordered, coerced value list."""
        from .types import coerce
        schema = info.schema
        if columns:
            provided = {c: v for c, v in zip(columns, raw_row)}
        else:
            if len(raw_row) != len(schema.columns):
                raise TransactionError("column count mismatch")
            provided = {c.name: v for c, v in zip(schema.columns, raw_row)}
        values = []
        for c in schema.columns:
            v = provided.get(c.name)
            values.append(coerce(c.type, v) if v is not None else None)
        return values

    def _insert(self, node: ast.Insert, txn):
        info = self.catalog.get(node.table)
        count = 0
        for raw_row in node.rows:
            values = self._row_values(info, node.columns, raw_row)
            record = info.schema.serialize(values)
            if self.isolation == "MVCC":
                self._insert_mvcc(info, values, record, txn)
            else:
                self._insert_2pl(info, values, record, txn)
            count += 1
        return f"{count} row(s) inserted"

    def _insert_2pl(self, info, values, record, txn):
        self._ensure_begin(txn)
        heap = self.heaps[info.name]
        rid = heap.insert(record)
        # lock the brand-new row, then log, then stamp page LSN (WAL rule)
        self.lock_mgr.acquire(txn.txn_id, (info.name, rid), LockMode.X)
        lsn = self.wal.append(kind=UPDATE, txn_id=txn.txn_id, table=info.name,
                              rid=rid, before=None, after=record)
        heap.set_lsn(rid, lsn)
        for col, tree in info.indexes.items():
            key = values[info.schema.index_of(col)]
            tree.insert(key, rid)
            txn.add_undo(lambda t=tree, k=key, r=rid: t.delete(k, r))
        info.n_tuples += 1

    def _insert_mvcc(self, info, values, record, txn):
        self._ensure_begin(txn)
        store = self.mvcc[info.name]
        rid = store.insert(record, txn)
        self.wal.append(kind=UPDATE, txn_id=txn.txn_id, table=info.name,
                        rid=rid, before=None, after=record)
        for col, tree in info.indexes.items():
            key = values[info.schema.index_of(col)]
            tree.insert(key, rid)
            txn.add_undo(lambda t=tree, k=key, r=rid: t.delete(k, r))
        info.n_tuples += 1

    def _delete(self, node: ast.Delete, txn):
        info = self.catalog.get(node.table)
        pred = self._compile_where(node.where, node.table) if node.where else None
        ctx = self._ctx(txn)

        # fast path: WHERE is a single equality on an indexed column ->
        # probe the B+ tree instead of scanning + locking the whole table.
        candidates = self._indexed_delete_candidates(info, node, txn)
        if candidates is None:
            candidates = self._scan_for_write(info, txn)

        victims = []
        for rid, raw in candidates:
            if raw is None:
                continue
            row = ctx.row_dict(node.table, info.schema, raw)
            if pred is None or pred.test(row):
                victims.append((rid, raw))
        for rid, raw in victims:
            self._ensure_begin(txn)
            if self.isolation == "MVCC":
                self.mvcc[node.table].update(rid, None, txn)
            else:
                self.lock_mgr.acquire(txn.txn_id, (node.table, rid), LockMode.X)
                lsn = self.wal.append(kind=UPDATE, txn_id=txn.txn_id,
                                      table=node.table, rid=rid,
                                      before=raw, after=None)
                self.heaps[node.table].delete(rid, lsn)
            vals = info.schema.deserialize(raw)
            for col, tree in info.indexes.items():
                tree.delete(vals[info.schema.index_of(col)], rid)
            info.n_tuples = max(0, info.n_tuples - 1)
        return f"{len(victims)} row(s) deleted"

    def _indexed_delete_candidates(self, info, node, txn):
        """If WHERE is exactly `col = value` on an indexed column, return an
        iterator of (rid, raw) for matching rows (locking only those rows in
        2PL). Otherwise return None to fall back to a full scan."""
        if not node.where or len(node.where.conjuncts) != 1:
            return None
        c = node.where.conjuncts[0]
        if c.op != "=":
            return None
        if not isinstance(c.left, ast.ColumnRef) or not isinstance(c.right, ast.Literal):
            return None
        col = c.left.name
        if col not in info.indexes:
            return None
        from .types import coerce
        key = coerce(info.schema.type_of(col), c.right.value)
        rids = list(info.indexes[col].search(key))

        def gen():
            for rid in rids:
                if self.isolation == "MVCC":
                    raw = self.mvcc[info.name].get(rid, txn)
                else:
                    self.lock_mgr.acquire(txn.txn_id, (info.name, rid), LockMode.S)
                    raw = self.heaps[info.name].get(rid)
                yield rid, raw
        return gen()

    def _scan_for_write(self, info, txn):
        if self.isolation == "MVCC":
            yield from self.mvcc[info.name].scan(txn)
        else:
            for rid, raw in self.heaps[info.name].scan():
                self.lock_mgr.acquire(txn.txn_id, (info.name, rid), LockMode.S)
                yield rid, raw

    # ---- SELECT ----------------------------------------------------------
    def _select(self, node: ast.Select, txn):
        plan = self.optimizer.build(node)
        ctx = self._ctx(txn)
        rows = list(plan.execute(ctx))
        columns = self._output_columns(plan)
        return Result(columns, rows)

    def explain(self, sql: str) -> str:
        node = Parser(sql).parse()
        if not isinstance(node, ast.Select):
            raise TransactionError("EXPLAIN only supports SELECT")
        plan = self.optimizer.build(node)
        return plan.explain()

    def _output_columns(self, plan) -> List[str]:
        # the top node is always a Project; read its labels
        out = getattr(plan, "out_cols", None)
        if out is not None:
            return [label for label, _ in out]
        return []

    # ---- helpers ---------------------------------------------------------
    def _ctx(self, txn: Transaction):
        return MVCCCtx(self, txn) if self.isolation == "MVCC" else TwoPLCtx(self, txn)

    def _compile_where(self, where: ast.Predicate, table: str) -> CompiledPredicate:
        conj = []
        for c in where.conjuncts:
            left = c.left
            right = c.right
            if isinstance(left, ast.ColumnRef):
                lkey = f"{table}.{left.name}"
                lc = True
            else:
                lkey = left.value if isinstance(left, ast.Literal) else left
                lc = False
            if isinstance(right, ast.ColumnRef):
                rkey = f"{table}.{right.name}"
                rc = True
            else:
                rkey = right.value if isinstance(right, ast.Literal) else right
                rc = False
            conj.append((lkey, c.op, rkey, lc, rc))
        return CompiledPredicate(conj)

    # ---- maintenance -----------------------------------------------------
    def analyze(self, table: Optional[str] = None):
        """Recompute optimizer statistics (cardinality, NDV, min/max) by
        scanning the live data source. Call after bulk loads so the cost model
        is sharp. In MVCC mode the source of truth is the version store, not
        the heap, so we scan a fresh snapshot of it."""
        names = [table] if table else list(self.catalog.tables.keys())
        for name in names:
            info = self.catalog.get(name)
            if self.isolation == "MVCC":
                probe = Transaction(-1, "MVCC", read_ts=self.clock.now())
                rows = (raw for _rid, raw in self.mvcc[name].scan(probe))
                self._analyze_from_rows(info, rows)
            else:
                self.catalog.analyze(name, self.heaps[name], info.schema.deserialize)

    def _analyze_from_rows(self, info, raw_rows):
        distinct = {c.name: set() for c in info.schema.columns}
        mins: Dict[str, Any] = {}
        maxs: Dict[str, Any] = {}
        n = 0
        for raw in raw_rows:
            vals = info.schema.deserialize(raw)
            n += 1
            for col, v in zip(info.schema.columns, vals):
                if v is None:
                    continue
                distinct[col.name].add(v)
                if col.type in (DataType.INT, DataType.FLOAT):
                    if col.name not in mins or v < mins[col.name]:
                        mins[col.name] = v
                    if col.name not in maxs or v > maxs[col.name]:
                        maxs[col.name] = v
        info.n_tuples = n
        info.col_stats = {
            c.name: ColumnStats(ndv=max(1, len(distinct[c.name])),
                                min=mins.get(c.name), max=maxs.get(c.name))
            for c in info.schema.columns
        }

    def checkpoint(self):
        self.wal.flush()
        self.bufferpool.flush_all()
        self.disk.fsync_all()
        active = [t for t in self._active]
        self.wal.append(kind="CHECKPOINT", active=active)
        self.wal.flush()

    def stats(self) -> Dict[str, Any]:
        s = {
            "isolation": self.isolation,
            "tables": len(self.catalog.tables),
            "buffer_pool": self.bufferpool.stats(),
            "wal_records": len(self.wal.records),
        }
        if self.isolation == "MVCC":
            s["mvcc"] = {t: st.stats() for t, st in self.mvcc.items()}
        return s

    def close(self):
        self.bufferpool.flush_all()
        self.disk.fsync_all()
        self.wal.flush()
        self._save_metadata()
        self.disk.close()


def connect(directory: str, isolation: str = "2PL", **kw) -> Database:
    return Database(directory, isolation=isolation, **kw)
