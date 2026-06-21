"""Statement executor.

Turns a parsed statement into actions against the catalog, heap files, indexes,
transaction, and (for SELECT) the optimizer's operator tree. Every data access
goes through the execution context so strict 2PL locks are taken consistently.

DDL (CREATE/DROP) is handled directly against the catalog. INSERT/DELETE
maintain every index alongside the heap and write WAL records through the
transaction. SELECT is optimized into a physical plan and pulled to completion.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, List, Optional

from ..catalog.schema import Column, DataType
from ..index.bplustree import DuplicateKeyError
from . import ast, plan
from .context import ExecutionContext
from .optimizer import Optimizer


class IntegrityError(Exception):
    pass


@dataclass
class ExecResult:
    kind: str                                  # 'select' | 'dml' | 'ddl'
    columns: List[str] = field(default_factory=list)
    rows: List[List[Any]] = field(default_factory=list)
    rowcount: int = 0
    message: str = ""

    def __str__(self) -> str:
        if self.kind == "select":
            return f"{len(self.rows)} row(s)"
        return self.message or f"{self.rowcount} row(s) affected"


class Executor:
    def execute(self, stmt, ctx: ExecutionContext) -> ExecResult:
        if isinstance(stmt, ast.CreateTable):
            return self._create_table(stmt, ctx)
        if isinstance(stmt, ast.CreateIndex):
            return self._create_index(stmt, ctx)
        if isinstance(stmt, ast.DropTable):
            ctx.catalog.drop_table(stmt.name)
            return ExecResult("ddl", message=f"table {stmt.name} dropped")
        if isinstance(stmt, ast.Insert):
            return self._insert(stmt, ctx)
        if isinstance(stmt, ast.Delete):
            return self._delete(stmt, ctx)
        if isinstance(stmt, ast.Select):
            return self._select(stmt, ctx)
        raise plan.ExecutionError(f"cannot execute {type(stmt).__name__}")

    # -- DDL ----------------------------------------------------------------
    def _create_table(self, stmt: ast.CreateTable, ctx: ExecutionContext) -> ExecResult:
        cols = [Column(c.name, DataType(c.type), nullable=not c.not_null)
                for c in stmt.columns]
        ctx.catalog.create_table(stmt.name, cols, pk_column=stmt.pk_column)
        return ExecResult("ddl", message=f"table {stmt.name} created")

    def _create_index(self, stmt: ast.CreateIndex, ctx: ExecutionContext) -> ExecResult:
        idx = ctx.catalog.create_index(stmt.table, stmt.column,
                                       unique=stmt.unique, name=stmt.name)
        return ExecResult("ddl", message=f"index {idx.name} created")

    # -- INSERT -------------------------------------------------------------
    def _insert(self, stmt: ast.Insert, ctx: ExecutionContext) -> ExecResult:
        table = ctx.catalog.get_table(stmt.table)
        ctx.lock_exclusive(table.name)
        schema = table.schema
        target_cols = stmt.columns or schema.names
        count = 0
        for values in stmt.rows:
            if len(values) != len(target_cols):
                raise plan.ExecutionError(
                    f"INSERT has {len(values)} values for {len(target_cols)} columns")
            row = {name: None for name in schema.names}
            for col, val in zip(target_cols, values):
                if not schema.has_column(col):
                    raise plan.ExecutionError(f"no such column {col!r}")
                row[col] = schema.coerce(col, val)

            # Enforce uniqueness *before* touching the heap so a violation
            # leaves no half-applied state.
            for idx in table.indexes.values():
                if idx.unique and idx.tree.contains(row[idx.column]):
                    raise IntegrityError(
                        f"duplicate key {row[idx.column]!r} for index {idx.name}")

            rid = table.heap.insert(schema.serialize(row), txn=ctx.txn)
            for idx in table.indexes.values():
                idx.tree.insert(row[idx.column], rid)
            count += 1
        return ExecResult("dml", rowcount=count,
                          message=f"{count} row(s) inserted")

    # -- DELETE -------------------------------------------------------------
    def _delete(self, stmt: ast.Delete, ctx: ExecutionContext) -> ExecResult:
        table = ctx.catalog.get_table(stmt.table)
        ctx.lock_exclusive(table.name)
        victims = list(self._matching_rids(table, stmt.where, ctx))
        for rid, row in victims:
            for idx in table.indexes.values():
                idx.tree.delete(row[idx.column], rid)
            table.heap.delete(rid, txn=ctx.txn)
        return ExecResult("dml", rowcount=len(victims),
                          message=f"{len(victims)} row(s) deleted")

    def _matching_rids(self, table, where: ast.Predicate, ctx: ExecutionContext):
        """Yield (rid, row_dict) for rows matching *where*, using an index when
        a sargable predicate allows it, else a full scan."""
        schema = table.schema
        # Try to use an index for one equality/range predicate.
        chosen = None
        for c in where.comparisons:
            if isinstance(c.right, ast.ColumnRef):
                continue
            if c.op in ("=", "<", "<=", ">", ">="):
                idx = table.index_on(c.left.name)
                if idx is not None:
                    chosen = (c, idx)
                    break

        def make_row(rec):
            base = schema.deserialize(rec)
            row = dict(base)
            row.update({f"{table.name}.{k}": v for k, v in base.items()})
            return base, row

        if chosen is not None:
            cmp, idx = chosen
            if cmp.op == "=":
                rids = list(idx.tree.search(cmp.right.value))
            elif cmp.op in (">", ">="):
                rids = [r for _k, r in idx.tree.range(
                    low=cmp.right.value, include_low=(cmp.op == ">="))]
            else:
                rids = [r for _k, r in idx.tree.range(
                    high=cmp.right.value, include_high=(cmp.op == "<="))]
            for rid in rids:
                rec = table.heap.get(rid)
                if rec is None:
                    continue
                base, row = make_row(rec)
                if plan.eval_predicate(row, where):
                    yield rid, base
        else:
            for rid, rec in table.heap.scan():
                base, row = make_row(rec)
                if plan.eval_predicate(row, where):
                    yield rid, base

    # -- SELECT -------------------------------------------------------------
    def _select(self, stmt: ast.Select, ctx: ExecutionContext) -> ExecResult:
        operator = Optimizer(ctx).build_select(stmt)
        labels = self._output_labels(stmt, ctx)
        rows: List[List[Any]] = []
        for row in operator.execute():
            rows.append([self._resolve_label(row, lab) for lab in labels])
        return ExecResult("select", columns=labels, rows=rows,
                          rowcount=len(rows))

    def _output_labels(self, stmt: ast.Select, ctx: ExecutionContext) -> List[str]:
        if stmt.columns != ["*"]:
            return list(stmt.columns)
        from_tbl = ctx.catalog.get_table(stmt.from_table)
        if not stmt.joins:
            return list(from_tbl.schema.names)
        labels = [f"{stmt.from_qualifier}.{c}" for c in from_tbl.schema.names]
        for j in stmt.joins:
            jt = ctx.catalog.get_table(j.table)
            labels += [f"{j.qualifier}.{c}" for c in jt.schema.names]
        return labels

    @staticmethod
    def _resolve_label(row, label: str):
        if "." in label and label not in row:
            tbl, col = label.split(".", 1)
            return plan.resolve(row, tbl, col)
        if label in row:
            return row[label]
        return plan.resolve(row, None, label)

    def explain(self, stmt: ast.Select, ctx: ExecutionContext) -> str:
        return Optimizer(ctx).explain(stmt)
