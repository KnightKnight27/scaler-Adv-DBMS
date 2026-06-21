"""Cost-based optimizer.

Two responsibilities, both demonstrable:

1. **Access-path selection** — for a single relation with local predicates,
   decide between a full ``SeqScan`` and an ``IndexScan``. We estimate the
   *selectivity* of each predicate (equality on a key = 1/distinct-values,
   ranges/inequalities use heuristics) and compare estimated costs:
   ``SeqScan`` ~ N rows, ``IndexScan`` ~ tree-height + matching rows. The
   cheaper wins, and any predicate the index didn't satisfy becomes a Filter.

2. **Join ordering** — for multi-way joins we build the join graph from the ON
   predicates and grow a left-deep nested-loop plan greedily, always adding the
   connected relation with the smallest estimated cardinality first, so the
   smaller inputs drive the joins.

The optimizer produces a tree of physical operators (``plan.py``) annotated
with row estimates, which ``EXPLAIN`` renders.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

from . import ast, plan
from .context import ExecutionContext

# Selectivity heuristics for predicates the statistics can't pin down exactly.
_RANGE_SELECTIVITY = 0.3
_EQ_SELECTIVITY_NO_INDEX = 0.1
_NEQ_SELECTIVITY = 0.9


@dataclass
class _Relation:
    qualifier: str
    table: Any                       # TableInfo
    local: List[ast.Comparison] = field(default_factory=list)
    est_rows: float = 0.0


class Optimizer:
    def __init__(self, ctx: ExecutionContext):
        self.ctx = ctx
        self._row_cache: Dict[str, int] = {}

    # -- statistics ---------------------------------------------------------
    def row_count(self, table) -> int:
        if table.name in self._row_cache:
            return self._row_cache[table.name]
        pk = table.primary_index()
        if pk is not None:
            n = len(pk.tree)
        elif table.indexes:
            n = len(next(iter(table.indexes.values())).tree)
        else:
            n = sum(1 for _ in table.heap.scan())
        n = max(1, n)
        self._row_cache[table.name] = n
        return n

    def selectivity(self, table, cmp: ast.Comparison) -> float:
        op = cmp.op
        if op == "!=":
            return _NEQ_SELECTIVITY
        if op in ("<", "<=", ">", ">="):
            return _RANGE_SELECTIVITY
        # equality
        idx = table.index_on(cmp.left.name)
        if idx is not None:
            ndv = max(1, idx.tree.num_keys())
            return 1.0 / ndv
        return _EQ_SELECTIVITY_NO_INDEX

    # -- access-path selection ---------------------------------------------
    def choose_access(self, rel: _Relation) -> Tuple[plan.Operator, float]:
        """Pick SeqScan vs IndexScan for one relation; return (op, est_rows)."""
        n = self.row_count(rel.table)
        local_sel = 1.0
        for c in rel.local:
            local_sel *= self.selectivity(rel.table, c)
        est_out = max(0.0, n * local_sel)

        # Look for the cheapest usable index predicate.
        best = None  # (cost, comparison, index)
        for c in rel.local:
            if c.op not in ("=", "<", "<=", ">", ">="):
                continue
            idx = rel.table.index_on(c.left.name)
            if idx is None:
                continue
            sel = self.selectivity(rel.table, c)
            cost = idx.tree.height() + max(1.0, n * sel)
            if best is None or cost < best[0]:
                best = (cost, c, idx)

        seq_cost = float(n)
        if best is not None and best[0] < seq_cost:
            _cost, cmp, idx = best
            scan: plan.Operator = plan.IndexScan(
                self.ctx, rel.table, rel.qualifier, idx, cmp.op, cmp.right.value)
            remaining = [c for c in rel.local if c is not cmp]
        else:
            scan = plan.SeqScan(self.ctx, rel.table, rel.qualifier)
            remaining = list(rel.local)

        if remaining:
            scan.est_rows = n              # scan-level estimate before filter
            op: plan.Operator = plan.Filter(scan, ast.Predicate(remaining))
            op.est_rows = est_out
        else:
            scan.est_rows = est_out
            op = scan
        return op, est_out

    # -- plan building ------------------------------------------------------
    def build_select(self, stmt: ast.Select) -> plan.Operator:
        relations = self._relations(stmt)
        qual_to_rel = {r.qualifier: r for r in relations}

        join_preds: List[ast.Comparison] = [j.on for j in stmt.joins]
        post_filters: List[ast.Comparison] = []

        # Distribute WHERE comparisons: literals push down to a relation;
        # column=column become extra join edges; the rest are post-filters.
        for c in stmt.where.comparisons:
            if isinstance(c.right, ast.ColumnRef):
                if c.op == "=":
                    join_preds.append(c)
                else:
                    post_filters.append(c)
                continue
            owner = self._owning_qualifier(c.left, relations)
            if owner is not None:
                qual_to_rel[owner].local.append(c)
            else:
                post_filters.append(c)

        for r in relations:
            r.est_rows = self._estimate_rows(r)

        root = self._build_join_tree(relations, join_preds, qual_to_rel)

        if post_filters:
            f = plan.Filter(root, ast.Predicate(post_filters))
            f.est_rows = root.est_rows
            root = f

        proj = plan.Projection(root, stmt.columns)
        proj.est_rows = root.est_rows
        return proj

    def _estimate_rows(self, rel: _Relation) -> float:
        n = self.row_count(rel.table)
        sel = 1.0
        for c in rel.local:
            sel *= self.selectivity(rel.table, c)
        return max(0.0, n * sel)

    def _build_join_tree(self, relations, join_preds, qual_to_rel) -> plan.Operator:
        # Single relation: just its access path.
        if len(relations) == 1:
            return self.choose_access(relations[0])[0]

        # Build adjacency from join predicates.
        edges: List[Tuple[str, str, ast.Comparison]] = []
        for c in join_preds:
            lq = self._owning_qualifier(c.left, relations)
            rq = self._owning_qualifier(c.right, relations) if isinstance(c.right, ast.ColumnRef) else None
            if lq and rq:
                edges.append((lq, rq, c))

        # Greedy: start from the smallest relation, then repeatedly add the
        # connected relation with the smallest estimate.
        ordered = sorted(relations, key=lambda r: r.est_rows)
        start = ordered[0]
        joined = {start.qualifier}
        current = self.choose_access(start)[0]
        current_est = start.est_rows
        used_edges = set()

        while len(joined) < len(relations):
            best = None  # (est, rel, edge)
            for q, table_rel in qual_to_rel.items():
                if q in joined:
                    continue
                edge = self._find_edge(edges, q, joined, used_edges)
                if edge is None:
                    continue
                est = current_est * table_rel.est_rows * self._join_sel(table_rel)
                if best is None or est < best[0]:
                    best = (est, table_rel, edge)
            if best is None:
                # Disconnected query (cartesian); attach remaining by smallest.
                rem = [r for r in ordered if r.qualifier not in joined]
                table_rel = rem[0]
                # No predicate: use a trivially-true self comparison guard.
                raise plan.ExecutionError(
                    "cartesian joins are not supported; add an ON/WHERE join condition")
            est, rel, edge = best
            right = self.choose_access(rel)[0]
            current = plan.NestedLoopJoin(current, right, edge[2])
            current.est_rows = max(1.0, est)
            current_est = current.est_rows
            joined.add(rel.qualifier)
            used_edges.add(id(edge[2]))
        return current

    @staticmethod
    def _find_edge(edges, q, joined, used_edges):
        for e in edges:
            if id(e[2]) in used_edges:
                continue
            a, b, _c = e
            if (a == q and b in joined) or (b == q and a in joined):
                return e
        return None

    def _join_sel(self, rel: _Relation) -> float:
        # Equi-join selectivity ~ 1/distinct values of the join column; without
        # column-level stats we approximate with 1/est_rows.
        return 1.0 / max(1.0, rel.est_rows)

    # -- helpers ------------------------------------------------------------
    def _relations(self, stmt: ast.Select) -> List[_Relation]:
        rels = [_Relation(stmt.from_qualifier,
                          self.ctx.catalog.get_table(stmt.from_table))]
        for j in stmt.joins:
            rels.append(_Relation(j.qualifier, self.ctx.catalog.get_table(j.table)))
        return rels

    @staticmethod
    def _owning_qualifier(ref: ast.ColumnRef, relations) -> Optional[str]:
        if ref.table is not None:
            for r in relations:
                if r.qualifier == ref.table:
                    return r.qualifier
            return None
        owners = [r.qualifier for r in relations if r.table.schema.has_column(ref.name)]
        return owners[0] if len(owners) == 1 else None

    def explain(self, stmt: ast.Select) -> str:
        return self.build_select(stmt).explain()
