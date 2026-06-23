"""
Physical query operators (Volcano / iterator model).

Each operator is a node with an `execute(ctx)` generator yielding "rows".
A row is a dict mapping a fully-qualified key ("table.column") to a value;
base scans also attach "__rid__.table" so downstream DELETE and row-level
locking can find the source tuple. The optimizer fills in `est_rows` and
`est_cost` on each node; `explain()` renders the chosen physical plan.

Concurrency is abstracted behind the execution context `ctx`, which exposes
scan_table / index_lookup / get_row honoring the active isolation mode (2PL
locking or MVCC snapshot), so operators are identical for both schemes.
"""
from __future__ import annotations

from typing import Any, Dict, Iterator, List, Optional, Tuple

OPS = {
    "=": lambda a, b: a == b,
    "<>": lambda a, b: a != b,
    "<": lambda a, b: a is not None and b is not None and a < b,
    "<=": lambda a, b: a is not None and b is not None and a <= b,
    ">": lambda a, b: a is not None and b is not None and a > b,
    ">=": lambda a, b: a is not None and b is not None and a >= b,
}


class CompiledPredicate:
    """A conjunction of comparisons over resolved column keys / literals."""

    def __init__(self, conjuncts: List[Tuple[Any, str, Any, bool, bool]]):
        # each conjunct: (left, op, right, left_is_col, right_is_col)
        self.conjuncts = conjuncts

    def test(self, row: Dict[str, Any]) -> bool:
        for left, op, right, lc, rc in self.conjuncts:
            lv = row.get(left) if lc else left
            rv = row.get(right) if rc else right
            if not OPS[op](lv, rv):
                return False
        return True


class PlanNode:
    est_rows: float = 0.0
    est_cost: float = 0.0

    def execute(self, ctx) -> Iterator[Dict[str, Any]]:
        raise NotImplementedError

    def explain(self, indent: int = 0) -> str:
        raise NotImplementedError


class SeqScan(PlanNode):
    def __init__(self, table: str, schema):
        self.table = table
        self.schema = schema

    def execute(self, ctx):
        for rid, raw in ctx.scan_table(self.table):
            row = ctx.row_dict(self.table, self.schema, raw)
            row[f"__rid__.{self.table}"] = rid
            yield row

    def explain(self, indent=0):
        pad = "  " * indent
        return (f"{pad}SeqScan({self.table})  "
                f"[rows~{self.est_rows:.0f} cost~{self.est_cost:.0f}]")


class IndexScan(PlanNode):
    def __init__(self, table: str, schema, column: str, low, high, desc: str):
        self.table = table
        self.schema = schema
        self.column = column
        self.low = low
        self.high = high
        self.desc = desc

    def execute(self, ctx):
        for rid in ctx.index_lookup(self.table, self.column, self.low, self.high):
            raw = ctx.get_row(self.table, rid)
            if raw is None:
                continue
            row = ctx.row_dict(self.table, self.schema, raw)
            row[f"__rid__.{self.table}"] = rid
            yield row

    def explain(self, indent=0):
        pad = "  " * indent
        return (f"{pad}IndexScan({self.table}.{self.column} {self.desc})  "
                f"[rows~{self.est_rows:.0f} cost~{self.est_cost:.0f}]")


class Filter(PlanNode):
    def __init__(self, child: PlanNode, pred: CompiledPredicate):
        self.child = child
        self.pred = pred

    def execute(self, ctx):
        for row in self.child.execute(ctx):
            if self.pred.test(row):
                yield row

    def explain(self, indent=0):
        pad = "  " * indent
        return (f"{pad}Filter  [rows~{self.est_rows:.0f}]\n"
                + self.child.explain(indent + 1))


class NestedLoopJoin(PlanNode):
    def __init__(self, left, right, lkey, rkey):
        self.left, self.right = left, right
        self.lkey, self.rkey = lkey, rkey

    def execute(self, ctx):
        right_rows = list(self.right.execute(ctx))
        for l in self.left.execute(ctx):
            lv = l.get(self.lkey)
            for r in right_rows:
                if lv == r.get(self.rkey):
                    merged = dict(l)
                    merged.update(r)
                    yield merged

    def explain(self, indent=0):
        pad = "  " * indent
        return (f"{pad}NestedLoopJoin({self.lkey} = {self.rkey})  "
                f"[rows~{self.est_rows:.0f} cost~{self.est_cost:.0f}]\n"
                + self.left.explain(indent + 1) + "\n"
                + self.right.explain(indent + 1))


class HashJoin(PlanNode):
    def __init__(self, left, right, lkey, rkey):
        # build side = right
        self.left, self.right = left, right
        self.lkey, self.rkey = lkey, rkey

    def execute(self, ctx):
        table: Dict[Any, List[Dict]] = {}
        for r in self.right.execute(ctx):
            table.setdefault(r.get(self.rkey), []).append(r)
        for l in self.left.execute(ctx):
            for r in table.get(l.get(self.lkey), ()):  # equi-join
                merged = dict(l)
                merged.update(r)
                yield merged

    def explain(self, indent=0):
        pad = "  " * indent
        return (f"{pad}HashJoin({self.lkey} = {self.rkey})  "
                f"[rows~{self.est_rows:.0f} cost~{self.est_cost:.0f}]\n"
                + self.left.explain(indent + 1) + "\n"
                + self.right.explain(indent + 1))


class Aggregate(PlanNode):
    def __init__(self, child, aggs: List[Tuple[str, str, Optional[str]]],
                 group_keys: List[str]):
        # aggs: list of (label, func, resolved_col_key or None)
        self.child = child
        self.aggs = aggs
        self.group_keys = group_keys

    def execute(self, ctx):
        groups: Dict[Tuple, List[Dict]] = {}
        order: List[Tuple] = []
        for row in self.child.execute(ctx):
            key = tuple(row.get(k) for k in self.group_keys)
            if key not in groups:
                groups[key] = []
                order.append(key)
            groups[key].append(row)
        for key in order:
            rows = groups[key]
            out: Dict[str, Any] = {}
            for gk, kv in zip(self.group_keys, key):
                out[gk] = kv
            for label, func, col in self.aggs:
                out[label] = _agg(func, rows, col)
            yield out

    def explain(self, indent=0):
        pad = "  " * indent
        gb = (" group_by=" + ",".join(self.group_keys)) if self.group_keys else ""
        return (f"{pad}Aggregate{gb}\n" + self.child.explain(indent + 1))


def _agg(func, rows, col):
    if func == "COUNT":
        if col is None:
            return len(rows)
        return sum(1 for r in rows if r.get(col) is not None)
    vals = [r.get(col) for r in rows if r.get(col) is not None]
    if not vals:
        return None
    if func == "SUM":
        return sum(vals)
    if func == "AVG":
        return sum(vals) / len(vals)
    if func == "MIN":
        return min(vals)
    if func == "MAX":
        return max(vals)
    raise ValueError(func)


class Project(PlanNode):
    def __init__(self, child, out_cols: List[Tuple[str, str]]):
        # out_cols: list of (label, source_key)
        self.child = child
        self.out_cols = out_cols

    def execute(self, ctx):
        for row in self.child.execute(ctx):
            yield {label: row.get(src) for label, src in self.out_cols}

    def explain(self, indent=0):
        pad = "  " * indent
        cols = ", ".join(l for l, _ in self.out_cols)
        return f"{pad}Project({cols})\n" + self.child.explain(indent + 1)
