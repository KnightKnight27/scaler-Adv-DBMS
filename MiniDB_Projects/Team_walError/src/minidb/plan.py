"""plan.py — a cost-based optimizer that turns a SELECT AST into a physical plan.

The optimizer makes three decisions, each driven by statistics from the catalog:

1. Access path per table: SeqScan vs IndexScan. If a table has a local predicate
   (col = value, or a range) on an indexed column, an IndexScan that touches only
   ~selectivity * N rows usually beats reading all N rows. An IndexScan may use
   loose bounds, so the precise local predicate is re-applied as a Filter.

2. Join order: relations are joined left-deep, greedily smallest-estimated-
   cardinality first, preferring a relation connected by a join predicate. In a
   nested-loop join the inner side is rescanned once per outer row, so putting the
   smaller relation on the outside minimizes total work.

3. Cardinality estimation: base row_count reduced by predicate selectivities
   (equality ~ 1/ndv, range ~ 0.3), used to compare candidate plans.

Each operator is annotated with `est_rows`/`est_cost`, surfaced by EXPLAIN.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .catalog import Catalog, Table
from . import sql as ast
from .executor import (
    Filter,
    IndexScan,
    NestedLoopJoin,
    Operator,
    Project,
    SeqScan,
)

DEFAULT_EQ_SEL = 0.1     # equality selectivity when no stats available
DEFAULT_RANGE_SEL = 0.3  # range (<, >, BETWEEN) selectivity
RANGE_OPS = {"<", "<=", ">", ">="}
_FLIP = {"<": ">", "<=": ">=", ">": "<", ">=": "<=", "=": "=", "!=": "!=", "<>": "<>"}


@dataclass
class _Rel:
    """A base relation participating in the query."""

    alias: str
    table: Table
    local: list[Any]          # conjuncts referencing only this relation
    est_rows: float = 0.0


# --- predicate utilities -----------------------------------------------------


def conjuncts(expr: Any) -> list[Any]:
    """Split a WHERE expression into its top-level AND-ed conjuncts."""
    if expr is None:
        return []
    if isinstance(expr, ast.And):
        return conjuncts(expr.left) + conjuncts(expr.right)
    return [expr]


def _alias_for(ref: ast.ColumnRef, rels: dict[str, Table]) -> str:
    if ref.table is not None:
        return ref.table
    # unqualified: find the single relation owning this column
    owners = [a for a, t in rels.items() if ref.name in t.schema.names]
    if len(owners) != 1:
        raise KeyError(f"cannot resolve column {ref.name!r} among {list(rels)}")
    return owners[0]


def referenced_aliases(expr: Any, rels: dict[str, Table]) -> set[str]:
    if isinstance(expr, ast.ColumnRef):
        return {_alias_for(expr, rels)}
    if isinstance(expr, ast.Comparison):
        return referenced_aliases(expr.left, rels) | referenced_aliases(expr.right, rels)
    if isinstance(expr, (ast.And, ast.Or)):
        return referenced_aliases(expr.left, rels) | referenced_aliases(expr.right, rels)
    return set()  # Literal


def as_col_op_lit(cmp: Any, rels: dict[str, Table]):
    """If `cmp` is `column <op> literal` (either order), return (alias, col, op, value)."""
    if not isinstance(cmp, ast.Comparison):
        return None
    left, right, op = cmp.left, cmp.right, cmp.op
    if isinstance(left, ast.ColumnRef) and isinstance(right, ast.Literal):
        col, lit = left, right
    elif isinstance(right, ast.ColumnRef) and isinstance(left, ast.Literal):
        col, lit, op = right, left, _FLIP[op]
    else:
        return None
    return _alias_for(col, rels), col.name, op, lit.value


# --- selectivity / cardinality ----------------------------------------------


def eq_selectivity(table: Table, column: str) -> float:
    idx = table.indexes.get(column)
    if idx is not None and idx.unique:
        # a unique index => at most one match => ndv == row_count
        return 1.0 / max(table.stats.row_count, 1)
    ndv = table.stats.ndv.get(column)
    return (1.0 / ndv) if ndv else DEFAULT_EQ_SEL


def local_selectivity(rel: _Rel, rels: dict[str, Table]) -> float:
    sel = 1.0
    for pred in rel.local:
        info = as_col_op_lit(pred, rels)
        if info is None:
            sel *= DEFAULT_RANGE_SEL  # opaque predicate (e.g. OR): rough guess
            continue
        _, col, op, _ = info
        sel *= eq_selectivity(rel.table, col) if op == "=" else DEFAULT_RANGE_SEL
    return sel


# --- access-path selection ---------------------------------------------------


def choose_access_path(rel: _Rel, rels: dict[str, Table]) -> Operator:
    """Pick SeqScan or IndexScan for one relation, then layer local Filters."""
    table = rel.table
    eq_pred = None      # (col, value) on an indexed column
    range_bounds: dict[str, list] = {}  # col -> [low, high]
    for pred in rel.local:
        info = as_col_op_lit(pred, rels)
        if info is None:
            continue
        _, col, op, value = info
        if not table.has_index(col):
            continue
        if op == "=":
            eq_pred = (col, value)
            break
        if op in RANGE_OPS:
            lo, hi = range_bounds.setdefault(col, [None, None])
            if op in (">", ">="):
                range_bounds[col][0] = value if lo is None else max(lo, value)
            else:
                range_bounds[col][1] = value if hi is None else min(hi, value)

    n = max(table.stats.row_count, 1)
    seq_cost = float(n)

    scan: Operator
    if eq_pred is not None:
        col, value = eq_pred
        scan = IndexScan(table, col, value, value, rel.alias)
        scan.est_rows = max(eq_selectivity(table, col) * n, 1.0)
        scan.est_cost = scan.est_rows + 1.0
        # equality via a unique/exact index is fully satisfied; drop that conjunct
        residual = [p for p in rel.local if as_col_op_lit(p, rels) != (rel.alias, col, "=", value)]
    elif range_bounds:
        col, (lo, hi) = next(iter(range_bounds.items()))
        scan = IndexScan(table, col, lo, hi, rel.alias)
        scan.est_rows = max(DEFAULT_RANGE_SEL * n, 1.0)
        scan.est_cost = scan.est_rows + 1.0
        residual = rel.local  # loose bounds -> re-check all local preds
    else:
        scan = SeqScan(table, rel.alias)
        scan.est_rows = float(n)
        scan.est_cost = seq_cost
        residual = rel.local

    # If somehow the index path isn't cheaper, fall back to SeqScan.
    if isinstance(scan, IndexScan) and scan.est_cost >= seq_cost:
        scan = SeqScan(table, rel.alias)
        scan.est_rows = float(n)
        scan.est_cost = seq_cost
        residual = rel.local

    op = scan
    if residual:
        pred = residual[0]
        for extra in residual[1:]:
            pred = ast.And(pred, extra)
        op = Filter(op, pred)
        op.est_rows = max(local_selectivity(rel, rels) * n, 1.0)
        op.est_cost = scan.est_cost
    return op


# --- join ordering -----------------------------------------------------------


def optimize(select: ast.Select, catalog: Catalog) -> Project:
    """Build a cost-based physical plan (Project at the root)."""
    # 1) collect base relations with aliases
    rels: dict[str, Table] = {}
    order: list[str] = []
    fa = select.from_alias or select.from_table
    rels[fa] = catalog.get_table(select.from_table)
    order.append(fa)
    for j in select.joins:
        ja = j.alias or j.table
        rels[ja] = catalog.get_table(j.table)
        order.append(ja)

    # 2) classify conjuncts: local (1 relation), join (>=2), constant (0)
    where_conjuncts = conjuncts(select.where)
    join_conjuncts = [jc.on for jc in select.joins]  # explicit ON predicates
    local_by_alias: dict[str, list] = {a: [] for a in order}
    other_conjuncts: list[Any] = []
    for c in where_conjuncts:
        refs = referenced_aliases(c, rels)
        if len(refs) == 1:
            local_by_alias[next(iter(refs))].append(c)
        elif len(refs) == 0:
            other_conjuncts.append(c)  # constant predicate
        else:
            join_conjuncts.append(c)  # WHERE-style join predicate

    rel_objs = {
        a: _Rel(a, rels[a], local_by_alias[a]) for a in order
    }
    for r in rel_objs.values():
        r.est_rows = max(local_selectivity(r, rels) * max(r.table.stats.row_count, 1), 1.0)

    # 3) greedy left-deep join ordering by estimated cardinality
    remaining = set(order)
    first = min(remaining, key=lambda a: rel_objs[a].est_rows)
    remaining.remove(first)
    joined = {first}
    root: Operator = choose_access_path(rel_objs[first], rels)
    used_joins: list[int] = []

    while remaining:
        # candidates connected to the joined set by a join predicate
        def connects(a: str) -> bool:
            for k, jc in enumerate(join_conjuncts):
                if k in used_joins:
                    continue
                refs = referenced_aliases(jc, rels)
                if a in refs and refs <= (joined | {a}):
                    return True
            return False

        connected = [a for a in remaining if connects(a)]
        pool = connected or list(remaining)
        nxt = min(pool, key=lambda a: rel_objs[a].est_rows)
        remaining.remove(nxt)

        inner = choose_access_path(rel_objs[nxt], rels)
        # gather join predicates now fully covered by joined ∪ {nxt}
        conds = []
        for k, jc in enumerate(join_conjuncts):
            if k in used_joins:
                continue
            refs = referenced_aliases(jc, rels)
            if nxt in refs and refs <= (joined | {nxt}):
                conds.append(jc)
                used_joins.append(k)
        on = None
        if conds:
            on = conds[0]
            for extra in conds[1:]:
                on = ast.And(on, extra)
        joined.add(nxt)
        join = NestedLoopJoin(root, inner, on)
        join.est_rows = max(root.est_rows * inner.est_rows * 0.3, 1.0) if conds \
            else root.est_rows * inner.est_rows
        join.est_cost = root.est_cost + root.est_rows * inner.est_cost
        root = join

    # 4) leftover predicates (constants, unconsumed) as a top Filter
    leftover = other_conjuncts + [
        join_conjuncts[k] for k in range(len(join_conjuncts)) if k not in used_joins
    ]
    if leftover:
        pred = leftover[0]
        for extra in leftover[1:]:
            pred = ast.And(pred, extra)
        root = Filter(root, pred)
        root.est_rows = getattr(root.child, "est_rows", 1.0)
        root.est_cost = getattr(root.child, "est_cost", 1.0)

    # 5) projection
    proj = Project(root, select.items, select.star)
    proj.est_rows = getattr(root, "est_rows", 1.0)
    proj.est_cost = getattr(root, "est_cost", 1.0)
    return proj


# --- EXPLAIN rendering -------------------------------------------------------


def format_plan(op: Operator, indent: int = 0) -> str:
    pad = "  " * indent
    est = ""
    if getattr(op, "est_rows", None) is not None:
        est = f"  (est_rows={op.est_rows:.0f}, est_cost={getattr(op, 'est_cost', 0):.0f})"
    head = op.explain(0).split("\n")[0]  # node's own label (first line)
    line = pad + head + est
    children = []
    for attr in ("child", "outer", "inner"):
        c = getattr(op, attr, None)
        if isinstance(c, Operator):
            children.append(format_plan(c, indent + 1))
    return "\n".join([line] + children)
