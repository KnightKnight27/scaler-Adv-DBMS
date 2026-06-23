"""
Query executor.
Implements volcano/iterator model: each operator has next() yielding rows.

Operators:
  SeqScan      — scan all pages of a heap file, yield visible rows
  IndexScan    — use B+ tree to find matching rows
  Filter       — apply WHERE conditions
  NestedLoopJoin — join two operators
  Projection   — select specific columns
  InsertOp     — insert a row
  DeleteOp     — mark rows deleted
"""
import json
from txn.transaction import Transaction
from txn.mvcc import MVCCManager
from storage.buffer_pool import BufferPool
from storage.page import Page
from catalog.catalog import TableSchema
from optimizer.optimizer import QueryPlan
from sql.parser import (
    SelectStmt, InsertStmt, DeleteStmt,
    Condition, JoinClause,
)


# ── helpers ───────────────────────────────────────────────────────────────────

def eval_condition(row: dict, cond: Condition) -> bool:
    col = cond.left.split('.')[-1]
    val = row.get(col)
    right = cond.right

    if val is None:
        return False

    # compare same types
    try:
        if isinstance(val, (int, float)) and isinstance(right, str):
            right = type(val)(right)
        elif isinstance(right, (int, float)) and isinstance(val, str):
            val = type(right)(val)
    except (ValueError, TypeError):
        pass

    ops = {
        '=': lambda a, b: a == b,
        '!=': lambda a, b: a != b,
        '<': lambda a, b: a < b,
        '>': lambda a, b: a > b,
        '<=': lambda a, b: a <= b,
        '>=': lambda a, b: a >= b,
    }
    fn = ops.get(cond.op)
    return fn(val, right) if fn else False


def apply_conditions(row: dict, conditions: list) -> bool:
    return all(eval_condition(row, c) for c in conditions)


def serialize_row(row: dict) -> bytes:
    return json.dumps(row, separators=(',', ':')).encode()


def deserialize_row(data: bytes) -> dict:
    return json.loads(data.decode())


# ── operators ─────────────────────────────────────────────────────────────────

class SeqScan:
    """Scan all pages, yield all MVCC-visible rows with their locations."""

    def __init__(self, bp: BufferPool, table: str, schema: TableSchema,
                 txn: Transaction, mvcc: MVCCManager):
        self.bp = bp
        self.table = table
        self.schema = schema
        self.txn = txn
        self.mvcc = mvcc

    def scan(self):
        """Yield (row_dict, page_id, slot_id) for all visible rows."""
        num_pages = self.bp.heap.num_pages
        for page_id in range(num_pages):
            page = self.bp.fetch_page(page_id)
            for slot_id, rec_bytes in page.all_records():
                row = deserialize_row(rec_bytes)
                if self.mvcc.is_visible_to_txn(row, self.txn):
                    yield row, page_id, slot_id
            self.bp.unpin_page(page_id)


class IndexScan:
    """Use B+ tree index to find rows matching a key equality."""

    def __init__(self, bp: BufferPool, tree, table: str, schema: TableSchema,
                 txn: Transaction, mvcc: MVCCManager,
                 key=None, low=None, high=None):
        self.bp = bp
        self.tree = tree
        self.table = table
        self.schema = schema
        self.txn = txn
        self.mvcc = mvcc
        self.key = key    # exact match
        self.low = low    # range scan start
        self.high = high  # range scan end

    def scan(self):
        if self.key is not None:
            loc = self.tree.search(self.key)
            if loc:
                yield from self._fetch(loc)
        else:
            lo = self.low if self.low is not None else float('-inf')
            hi = self.high if self.high is not None else float('inf')
            for _, loc in self.tree.range_scan(lo, hi):
                yield from self._fetch(loc)

    def _fetch(self, loc):
        page_id, slot_id = loc
        page = self.bp.fetch_page(page_id)
        rec = page.get_record(slot_id)
        self.bp.unpin_page(page_id)
        if rec:
            row = deserialize_row(rec)
            if self.mvcc.is_visible_to_txn(row, self.txn):
                yield row, page_id, slot_id


class Filter:
    """Apply WHERE conditions to a scan stream."""

    def __init__(self, source, conditions: list):
        self.source = source
        self.conditions = conditions

    def scan(self):
        for row, page_id, slot_id in self.source.scan():
            clean = MVCCManager.strip_system_fields(row)
            if apply_conditions(clean, self.conditions):
                yield row, page_id, slot_id


class NestedLoopJoin:
    """
    Nested-loop join: for each outer row, scan all inner rows.
    On-condition: left_col = right_col.
    """

    def __init__(self, outer, inner_factory, join: JoinClause):
        self.outer = outer
        self.inner_factory = inner_factory  # callable() -> inner scan operator
        self.join = join

    def scan(self):
        left_col = self.join.left_col.split('.')[-1]
        right_col = self.join.right_col.split('.')[-1]

        for outer_row, outer_page, outer_slot in self.outer.scan():
            outer_clean = MVCCManager.strip_system_fields(outer_row)
            inner = self.inner_factory()
            for inner_row, inner_page, inner_slot in inner.scan():
                inner_clean = MVCCManager.strip_system_fields(inner_row)
                if outer_clean.get(left_col) == inner_clean.get(right_col):
                    combined = {**outer_clean, **inner_clean}
                    yield combined, outer_page, outer_slot


class Projection:
    """Select only specific columns from a scan stream."""

    def __init__(self, source, columns: list[str]):
        self.source = source
        self.columns = columns

    def scan(self):
        for row, page_id, slot_id in self.source.scan():
            clean = MVCCManager.strip_system_fields(row) if '_xmin' in row else row
            if self.columns == ['*']:
                yield clean, page_id, slot_id
            else:
                projected = {c.split('.')[-1]: clean.get(c.split('.')[-1])
                             for c in self.columns}
                yield projected, page_id, slot_id


# ── executor ──────────────────────────────────────────────────────────────────

class Executor:
    def __init__(self, table_registry: dict, mvcc: MVCCManager, wal, optimizer):
        """
        table_registry: {table_name: {'bp': BufferPool, 'schema': TableSchema,
                                       'index': BPlusTree or None}}
        """
        self.registry = table_registry
        self.mvcc = mvcc
        self.wal = wal
        self.optimizer = optimizer
        self._free_page_hint: dict[str, int] = {}  # table -> last page with free space

    def execute(self, stmt, txn: Transaction) -> list[dict]:
        if isinstance(stmt, SelectStmt):
            return self._exec_select(stmt, txn)
        if isinstance(stmt, InsertStmt):
            return self._exec_insert(stmt, txn)
        if isinstance(stmt, DeleteStmt):
            return self._exec_delete(stmt, txn)
        raise ValueError(f"Unsupported statement: {type(stmt)}")

    # ── SELECT ────────────────────────────────────────────────────────────────

    def _exec_select(self, stmt: SelectStmt, txn: Transaction) -> list[dict]:
        reg = self.registry
        tables = [stmt.table] + [j.table for j in stmt.joins]

        # optimizer
        indexes = {t: {reg[t]['index_col']} if reg[t].get('index_col') else set()
                   for t in tables if t in reg}
        plan = self.optimizer.optimize(stmt, indexes)

        # build scan for primary table
        def make_scan(table, conditions):
            entry = reg[table]
            bp = entry['bp']
            schema = entry['schema']
            tree = entry.get('tree')

            # try index scan
            if plan.table_scans.get(table) == 'index' and tree:
                idx_col = plan.index_cols.get(table)
                for cond in conditions:
                    if cond.left.split('.')[-1] == idx_col and cond.op == '=':
                        base = IndexScan(bp, tree, table, schema, txn, self.mvcc,
                                         key=cond.right)
                        remaining = [c for c in conditions if c != cond]
                        return Filter(base, remaining) if remaining else base

            base = SeqScan(bp, table, schema, txn, self.mvcc)
            return Filter(base, conditions) if conditions else base

        # group WHERE conditions by table
        def group_conds(conditions, tables):
            result = {t: [] for t in tables}
            for cond in conditions:
                col = cond.left
                if '.' in col:
                    tbl = col.split('.')[0]
                    if tbl in result:
                        result[tbl].append(cond)
                        continue
                result[tables[0]].append(cond)
            return result

        cond_by_table = group_conds(stmt.where, tables)
        scan = make_scan(stmt.table, cond_by_table.get(stmt.table, []))

        # chain joins
        for join in stmt.joins:
            join_table = join.table
            inner_conds = cond_by_table.get(join_table, [])
            outer = scan

            def make_inner(jt=join_table, jc=inner_conds):
                return make_scan(jt, jc)

            scan = NestedLoopJoin(outer, make_inner, join)

        proj = Projection(scan, stmt.columns)

        results = []
        for row, _, _ in proj.scan():
            results.append(row)

        # order by
        if stmt.order_by:
            col, direction = stmt.order_by
            reverse = direction == 'DESC'
            results.sort(key=lambda r: r.get(col.split('.')[-1], 0), reverse=reverse)

        return results

    # ── INSERT ────────────────────────────────────────────────────────────────

    def _exec_insert(self, stmt: InsertStmt, txn: Transaction) -> list[dict]:
        entry = self.registry[stmt.table]
        bp: BufferPool = entry['bp']
        schema = entry['schema']
        tree = entry.get('tree')
        pk_col = entry.get('pk_col')

        # build data dict
        data = dict(zip(stmt.columns, stmt.values))
        data = schema.cast_row(data)

        # wrap with MVCC fields
        row = self.mvcc.new_row(data, txn.txid)
        rec = serialize_row(row)

        # find page with space, or allocate new
        page_id, slot_id = self._insert_into_heap(bp, rec, stmt.table)

        # update B+ tree index on primary key
        if tree and pk_col and pk_col in data:
            tree.insert(data[pk_col], (page_id, slot_id))

        # WAL
        self.wal.log_insert(txn.txid, stmt.table, page_id, slot_id, row)

        # undo: mark deleted on rollback
        txn.add_undo({'op': 'undo_insert', 'table': stmt.table,
                      'page_id': page_id, 'slot_id': slot_id})

        bp.unpin_page(page_id, dirty=True)
        return [{'inserted': 1}]

    def _insert_into_heap(self, bp: BufferPool, rec: bytes, table: str = '') -> tuple[int, int]:
        # start from last known free page — avoids O(n) scan on bulk inserts
        hint = self._free_page_hint.get(table, max(0, bp.heap.num_pages - 1))
        for page_id in range(hint, bp.heap.num_pages):
            page = bp.fetch_page(page_id)
            slot_id = page.insert_record(rec)
            if slot_id is not None:
                self._free_page_hint[table] = page_id
                return page_id, slot_id
            bp.unpin_page(page_id)
        # allocate new page
        page = bp.new_page()
        slot_id = page.insert_record(rec)
        self._free_page_hint[table] = page.page_id
        return page.page_id, slot_id

    # ── DELETE ────────────────────────────────────────────────────────────────

    def _exec_delete(self, stmt: DeleteStmt, txn: Transaction) -> list[dict]:
        entry = self.registry[stmt.table]
        bp: BufferPool = entry['bp']
        schema = entry['schema']
        tree = entry.get('tree')
        pk_col = entry.get('pk_col')
        mvcc = self.mvcc

        deleted_count = 0
        to_delete = []

        # collect visible rows matching WHERE
        scanner = SeqScan(bp, stmt.table, schema, txn, mvcc)
        for row, page_id, slot_id in scanner.scan():
            clean = MVCCManager.strip_system_fields(row)
            if apply_conditions(clean, stmt.where):
                if not mvcc.can_write(row, txn):
                    raise RuntimeError("Write conflict: concurrent transaction modified this row")
                to_delete.append((row, page_id, slot_id, clean))

        for row, page_id, slot_id, clean in to_delete:
            # MVCC delete: write new version with _xmax set
            deleted_row = mvcc.mark_deleted(row, txn.txid)
            rec = serialize_row(deleted_row)

            page = bp.fetch_page(page_id)
            updated = page.update_record(slot_id, rec)
            if not updated:
                # doesn't fit — delete and reinsert
                page.delete_record(slot_id)
                new_page_id, new_slot_id = self._insert_into_heap(bp, rec)
                bp.unpin_page(new_page_id, dirty=True)
                if tree and pk_col and pk_col in clean:
                    tree.delete(clean[pk_col])
            else:
                bp.unpin_page(page_id, dirty=True)
                if tree and pk_col and pk_col in clean:
                    tree.delete(clean[pk_col])

            self.wal.log_delete(txn.txid, stmt.table, page_id, slot_id, deleted_row)
            txn.add_undo({'op': 'undo_delete', 'table': stmt.table,
                          'page_id': page_id, 'slot_id': slot_id, 'row': row})
            deleted_count += 1

        return [{'deleted': deleted_count}]
