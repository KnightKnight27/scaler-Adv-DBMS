"""executor.py — Volcano-model physical operators and statement execution.

Each operator implements the classic iterator interface:
    open()  -> start producing
    next()  -> return the next Row, or None when exhausted
    close() -> release resources
Operators compose into a tree; the root is pulled tuple-by-tuple. This is the
"Volcano" / iterator execution model.

A Row carries both the output `values` and a `rids` map (alias -> RID) recording
which physical record each value came from, so DELETE can reuse the same
scan/filter pipeline as SELECT and then delete by RID.

This module also provides a NAIVE plan builder (SeqScan + nested-loop joins) and
the DDL/DML executors. plan.py later adds a cost-based optimizer that builds
better trees from the same operators (e.g. IndexScan instead of SeqScan).
"""

from __future__ import annotations

from typing import Any, Callable, Iterator

from .catalog import Catalog, Table
from .engine import MiniDBError, Result
from .heap import RID
from . import sql as ast
from .types import Column, Schema


class ExecutionError(MiniDBError):
    """Raised on a semantic/runtime error during execution."""


# =============================================================================
# Row + predicate evaluation
# =============================================================================


class Row:
    """A tuple flowing through the operator tree."""

    __slots__ = ("values", "rids")

    def __init__(self, values: list[Any], rids: dict[str, RID] | None = None) -> None:
        self.values = values
        self.rids = rids or {}

    def merge(self, other: "Row") -> "Row":
        return Row(self.values + other.values, {**self.rids, **other.rids})


# A column descriptor in an operator's output schema: (table_alias, column_name)
ColDesc = tuple[str | None, str]


def resolve_column(columns: list[ColDesc], ref: ast.ColumnRef) -> int:
    matches = [
        i
        for i, (tbl, name) in enumerate(columns)
        if name == ref.name and (ref.table is None or tbl == ref.table)
    ]
    if not matches:
        qual = f"{ref.table}.{ref.name}" if ref.table else ref.name
        raise ExecutionError(f"unknown column: {qual}")
    if len(matches) > 1:
        raise ExecutionError(f"ambiguous column reference: {ref.name}")
    return matches[0]


_CMP = {
    "=": lambda a, b: a == b,
    "==": lambda a, b: a == b,
    "<>": lambda a, b: a != b,
    "!=": lambda a, b: a != b,
    "<": lambda a, b: a < b,
    "<=": lambda a, b: a <= b,
    ">": lambda a, b: a > b,
    ">=": lambda a, b: a >= b,
}


def evaluate(expr: Any, row: Row, columns: list[ColDesc]) -> Any:
    """Evaluate a WHERE/ON expression against a row. NULLs make predicates False."""
    if isinstance(expr, ast.Literal):
        return expr.value
    if isinstance(expr, ast.ColumnRef):
        return row.values[resolve_column(columns, expr)]
    if isinstance(expr, ast.Comparison):
        left = evaluate(expr.left, row, columns)
        right = evaluate(expr.right, row, columns)
        if left is None or right is None:  # SQL NULL comparison -> unknown -> False
            return False
        return _CMP[expr.op](left, right)
    if isinstance(expr, ast.And):
        return bool(evaluate(expr.left, row, columns)) and bool(
            evaluate(expr.right, row, columns)
        )
    if isinstance(expr, ast.Or):
        return bool(evaluate(expr.left, row, columns)) or bool(
            evaluate(expr.right, row, columns)
        )
    raise ExecutionError(f"cannot evaluate expression: {expr!r}")


# =============================================================================
# Operators
# =============================================================================


class Operator:
    """Base Volcano operator. Subclasses implement `_rows()` as a generator."""

    columns: list[ColDesc] = []

    def _rows(self) -> Iterator[Row]:
        raise NotImplementedError

    def open(self) -> None:
        self._gen = self._rows()

    def next(self) -> Row | None:
        return next(self._gen, None)

    def close(self) -> None:
        self._gen = None

    def __iter__(self) -> Iterator[Row]:
        self.open()
        while (r := self.next()) is not None:
            yield r
        self.close()

    def explain(self, indent: int = 0) -> str:
        return "  " * indent + self.__class__.__name__


class SeqScan(Operator):
    """Sequential scan over a table's heap."""

    def __init__(self, table: Table, alias: str | None = None) -> None:
        self.table = table
        self.alias = alias or table.name
        self.columns = [(self.alias, c) for c in table.schema.names]

    def _rows(self) -> Iterator[Row]:
        for rid, values in self.table.scan():
            yield Row(list(values), {self.alias: rid})

    def explain(self, indent: int = 0) -> str:
        return "  " * indent + f"SeqScan({self.table.name} AS {self.alias})"


class IndexScan(Operator):
    """Index scan: use a column's B+ tree to fetch only matching RIDs.

    `low`/`high` are inclusive bounds (low==high => equality lookup). A None
    bound is unbounded. This is what the optimizer picks over SeqScan when a
    sargable predicate hits an indexed column.
    """

    def __init__(
        self,
        table: Table,
        column: str,
        low: Any,
        high: Any,
        alias: str | None = None,
    ) -> None:
        self.table = table
        self.alias = alias or table.name
        self.column = column
        self.low = low
        self.high = high
        self.columns = [(self.alias, c) for c in table.schema.names]

    def _rows(self) -> Iterator[Row]:
        if self.low is not None and self.low == self.high:
            rids = self.table.index_lookup(self.column, self.low)
        else:
            rids = self.table.index_range(self.column, self.low, self.high)
        for rid in rids:
            rec = self.table.heap.get(rid)
            if rec is not None:
                yield Row(list(self.table.schema.decode(rec)), {self.alias: rid})

    def explain(self, indent: int = 0) -> str:
        bound = (
            f"{self.column}={self.low}"
            if self.low == self.high
            else f"{self.low}<={self.column}<={self.high}"
        )
        return "  " * indent + f"IndexScan({self.table.name} AS {self.alias}, {bound})"


class Filter(Operator):
    def __init__(self, child: Operator, predicate: Any) -> None:
        self.child = child
        self.predicate = predicate
        self.columns = child.columns

    def _rows(self) -> Iterator[Row]:
        self.child.open()
        while (r := self.child.next()) is not None:
            if evaluate(self.predicate, r, self.columns):
                yield r
        self.child.close()

    def explain(self, indent: int = 0) -> str:
        return "  " * indent + "Filter\n" + self.child.explain(indent + 1)


class NestedLoopJoin(Operator):
    """Inner nested-loop join: for each outer row, rescan the inner side."""

    def __init__(self, outer: Operator, inner: Operator, on: Any) -> None:
        self.outer = outer
        self.inner = inner
        self.on = on
        self.columns = outer.columns + inner.columns

    def _rows(self) -> Iterator[Row]:
        self.outer.open()
        while (o := self.outer.next()) is not None:
            self.inner.open()
            while (i := self.inner.next()) is not None:
                combined = o.merge(i)
                if self.on is None or evaluate(self.on, combined, self.columns):
                    yield combined
            self.inner.close()
        self.outer.close()

    def explain(self, indent: int = 0) -> str:
        return (
            "  " * indent
            + "NestedLoopJoin\n"
            + self.outer.explain(indent + 1)
            + "\n"
            + self.inner.explain(indent + 1)
        )


class Project(Operator):
    """Projection: select/compute output columns (or pass through on '*')."""

    def __init__(self, child: Operator, items: list[ast.SelectItem], star: bool) -> None:
        self.child = child
        self.items = items
        self.star = star
        if star:
            self.columns = child.columns
            self.output_names = [name for _, name in child.columns]
        else:
            self.columns = [(None, it.alias or it.expr.name) for it in items]
            self.output_names = [it.alias or it.expr.name for it in items]

    def _rows(self) -> Iterator[Row]:
        self.child.open()
        while (r := self.child.next()) is not None:
            if self.star:
                yield Row(list(r.values), dict(r.rids))
            else:
                vals = [evaluate(it.expr, r, self.child.columns) for it in self.items]
                yield Row(vals, dict(r.rids))
        self.child.close()

    def explain(self, indent: int = 0) -> str:
        cols = "*" if self.star else ", ".join(self.output_names)
        return "  " * indent + f"Project({cols})\n" + self.child.explain(indent + 1)


# =============================================================================
# Naive plan builder (baseline; plan.py adds the cost-based optimizer)
# =============================================================================


def build_naive_plan(select: ast.Select, catalog: Catalog) -> Project:
    """SeqScan everything, left-deep nested-loop joins, then Filter, then Project."""
    root: Operator = SeqScan(
        catalog.get_table(select.from_table), select.from_alias
    )
    for j in select.joins:
        inner = SeqScan(catalog.get_table(j.table), j.alias)
        root = NestedLoopJoin(root, inner, j.on)
    if select.where is not None:
        root = Filter(root, select.where)
    return Project(root, select.items, select.star)


def materialize(project: Project) -> Result:
    rows = [tuple(r.values) for r in project]
    return Result(columns=list(project.output_names), rows=rows, rowcount=len(rows))


# =============================================================================
# Statement execution (DDL/DML). plan.py/engine wire the optimized SELECT path.
# =============================================================================


def exec_create_table(stmt: ast.CreateTable, catalog: Catalog) -> Result:
    pk_count = sum(1 for c in stmt.columns if c.primary_key)
    if pk_count > 1:
        raise ExecutionError("only one PRIMARY KEY column is supported")
    schema = Schema(
        [
            Column(c.name, c.type, nullable=c.nullable, primary_key=c.primary_key)
            for c in stmt.columns
        ]
    )
    catalog.create_table(stmt.table, schema)
    return Result(message=f"CREATE TABLE {stmt.table}")


def exec_create_index(stmt: ast.CreateIndex, catalog: Catalog) -> Result:
    table = catalog.get_table(stmt.table)
    table.create_index(stmt.column)
    return Result(message=f"CREATE INDEX ON {stmt.table}({stmt.column})")


def _row_in_schema_order(table: Table, columns: list[str] | None, values: list[Any]):
    schema = table.schema
    if columns is None:
        if len(values) != len(schema):
            raise ExecutionError(
                f"table {table.name} has {len(schema)} columns, got {len(values)} values"
            )
        return tuple(values)
    if len(columns) != len(values):
        raise ExecutionError("column count does not match value count")
    by_name = dict(zip(columns, values))
    out = []
    for col in schema.columns:
        if col.name in by_name:
            out.append(by_name[col.name])
        elif not col.nullable:
            raise ExecutionError(f"missing value for NOT NULL column {col.name!r}")
        else:
            out.append(None)
    return tuple(out)


def exec_insert(stmt: ast.Insert, catalog: Catalog) -> Result:
    table = catalog.get_table(stmt.table)
    count = 0
    for values in stmt.rows:
        row = _row_in_schema_order(table, stmt.columns, values)
        table.insert_row(row)
        count += 1
    return Result(rowcount=count, message=f"{count} row(s) inserted")


def exec_delete(stmt: ast.Delete, catalog: Catalog) -> Result:
    table = catalog.get_table(stmt.table)
    alias = stmt.table
    scan: Operator = SeqScan(table, alias)
    if stmt.where is not None:
        scan = Filter(scan, stmt.where)
    victims: list[tuple[RID, tuple]] = [
        (r.rids[alias], tuple(r.values)) for r in scan
    ]
    count = 0
    for rid, values in victims:
        if table.delete_by_rid(rid, values):
            count += 1
    return Result(rowcount=count, message=f"{count} row(s) deleted")


def exec_select(select: ast.Select, catalog: Catalog) -> Result:
    return materialize(build_naive_plan(select, catalog))
