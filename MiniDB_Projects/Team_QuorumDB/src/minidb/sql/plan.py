"""Physical query operators (the Volcano / iterator model).

Each operator implements ``execute()`` as a generator of *rows*. A row is a
dict keyed by qualified column name (``"qualifier.column"``), so columns from
different tables in a join never collide. Operators compose into a tree; the
optimizer decides which tree to build and the executor pulls rows from the
root.

    SeqScan          full heap scan of a table
    IndexScan        B+Tree lookup (equality) or range scan
    Filter           keep rows satisfying a predicate
    NestedLoopJoin   block nested-loop equi-join (right side materialised)
    Projection       select/relabel output columns

Each operator carries ``est_rows`` (filled in by the optimizer) and renders an
EXPLAIN line, so a plan can describe itself.
"""

from __future__ import annotations

from typing import Any, Dict, Iterator, List, Optional

from . import ast

Row = Dict[str, Any]

_OPS = {
    "=": lambda a, b: a == b,
    "!=": lambda a, b: a != b,
    "<": lambda a, b: a < b,
    "<=": lambda a, b: a <= b,
    ">": lambda a, b: a > b,
    ">=": lambda a, b: a >= b,
}


class ExecutionError(Exception):
    pass


# -- row helpers -----------------------------------------------------------
def resolve(row: Row, table: Optional[str], name: str) -> Any:
    if table is not None:
        key = f"{table}.{name}"
        if key in row:
            return row[key]
        raise ExecutionError(f"unknown column {key}")
    if name in row:
        return row[name]
    matches = [v for k, v in row.items() if k.rsplit(".", 1)[-1] == name]
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise ExecutionError(f"unknown column {name!r}")
    raise ExecutionError(f"ambiguous column {name!r}")


def _resolve_ref(row: Row, ref: ast.ColumnRef) -> Any:
    return resolve(row, ref.table, ref.name)


def eval_comparison(row: Row, cmp: ast.Comparison) -> bool:
    left = _resolve_ref(row, cmp.left)
    right = (_resolve_ref(row, cmp.right) if isinstance(cmp.right, ast.ColumnRef)
             else cmp.right.value)
    if left is None or right is None:      # SQL NULL comparison -> unknown/false
        return False
    return _OPS[cmp.op](left, right)


def eval_predicate(row: Row, predicate: ast.Predicate) -> bool:
    return all(eval_comparison(row, c) for c in predicate.comparisons)


def _qualify(qualifier: str, values: Dict[str, Any]) -> Row:
    return {f"{qualifier}.{col}": val for col, val in values.items()}


# -- operators -------------------------------------------------------------
class Operator:
    name = "Operator"
    est_rows: float = 0.0

    def execute(self) -> Iterator[Row]:
        raise NotImplementedError

    def detail(self) -> str:
        return ""

    def children(self) -> List["Operator"]:
        return []

    def explain(self, indent: int = 0) -> str:
        pad = "  " * indent
        det = self.detail()
        head = f"{pad}{self.name}" + (f"  [{det}]" if det else "")
        head += f"  (est_rows={self.est_rows:g})"
        lines = [head]
        for child in self.children():
            lines.append(child.explain(indent + 1))
        return "\n".join(lines)


class SeqScan(Operator):
    name = "SeqScan"

    def __init__(self, ctx, table_info, qualifier: str):
        self.ctx = ctx
        self.table = table_info
        self.qualifier = qualifier

    def execute(self) -> Iterator[Row]:
        self.ctx.lock_shared(self.table.name)
        schema = self.table.schema
        for _rid, rec in self.table.heap.scan():
            yield _qualify(self.qualifier, schema.deserialize(rec))

    def detail(self) -> str:
        return f"{self.table.name} AS {self.qualifier}"


class IndexScan(Operator):
    name = "IndexScan"

    def __init__(self, ctx, table_info, qualifier: str, index_info,
                 op: str, key: Any):
        self.ctx = ctx
        self.table = table_info
        self.qualifier = qualifier
        self.index = index_info
        self.op = op
        self.key = key

    def _rids(self):
        tree = self.index.tree
        if self.op == "=":
            return list(tree.search(self.key))
        if self.op in (">", ">="):
            return [rid for _k, rid in tree.range(
                low=self.key, include_low=(self.op == ">="))]
        if self.op in ("<", "<="):
            return [rid for _k, rid in tree.range(
                high=self.key, include_high=(self.op == "<="))]
        raise ExecutionError(f"index scan cannot use operator {self.op!r}")

    def execute(self) -> Iterator[Row]:
        self.ctx.lock_shared(self.table.name)
        schema = self.table.schema
        for rid in self._rids():
            rec = self.table.heap.get(rid)
            if rec is not None:
                yield _qualify(self.qualifier, schema.deserialize(rec))

    def detail(self) -> str:
        return (f"{self.table.name} AS {self.qualifier} "
                f"using {self.index.name} ({self.index.column} {self.op} {self.key!r})")


class Filter(Operator):
    name = "Filter"

    def __init__(self, child: Operator, predicate: ast.Predicate):
        self.child = child
        self.predicate = predicate

    def execute(self) -> Iterator[Row]:
        for row in self.child.execute():
            if eval_predicate(row, self.predicate):
                yield row

    def children(self) -> List[Operator]:
        return [self.child]

    def detail(self) -> str:
        return " AND ".join(
            f"{c.left.qualified} {c.op} "
            f"{(c.right.qualified if isinstance(c.right, ast.ColumnRef) else repr(c.right.value))}"
            for c in self.predicate.comparisons)


class NestedLoopJoin(Operator):
    name = "NestedLoopJoin"

    def __init__(self, left: Operator, right: Operator, on: ast.Comparison):
        self.left = left
        self.right = right
        self.on = on

    def execute(self) -> Iterator[Row]:
        right_rows = list(self.right.execute())   # materialise inner side once
        for l in self.left.execute():
            for r in right_rows:
                merged = {**l, **r}
                if eval_comparison(merged, self.on):
                    yield merged

    def children(self) -> List[Operator]:
        return [self.left, self.right]

    def detail(self) -> str:
        return f"{self.on.left.qualified} = {self.on.right.qualified}"


class Projection(Operator):
    name = "Projection"

    def __init__(self, child: Operator, columns: List[str]):
        self.child = child
        self.columns = columns      # [] / ["*"] handled by caller as passthrough

    def execute(self) -> Iterator[Row]:
        if not self.columns or self.columns == ["*"]:
            yield from self.child.execute()
            return
        for row in self.child.execute():
            out: Row = {}
            for label in self.columns:
                if "." in label:
                    tbl, col = label.split(".", 1)
                    out[label] = resolve(row, tbl, col)
                else:
                    out[label] = resolve(row, None, label)
            yield out

    def children(self) -> List[Operator]:
        return [self.child]

    def detail(self) -> str:
        return ", ".join(self.columns)
