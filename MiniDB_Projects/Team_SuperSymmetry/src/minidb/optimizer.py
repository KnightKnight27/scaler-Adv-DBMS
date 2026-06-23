"""
Cost-based optimizer.

Turns a parsed SELECT into a physical plan of operators. Responsibilities:
  * resolve columns / aliases to fully-qualified keys
  * push single-table predicates down to the scan
  * choose between a SeqScan and an IndexScan using estimated cost
  * estimate per-node cardinalities via selectivity estimation
  * order joins greedily (cheapest intermediate result first) and pick
    HashJoin for equi-joins (build side = smaller estimated input)

The cost/selectivity model is deliberately simple and textbook-style so it
can be explained: equality selectivity = 1/NDV, range selectivity from
min/max, independence assumption across ANDed predicates.
"""
from __future__ import annotations

import math
from typing import Any, Dict, List, Optional, Tuple

from . import sql as ast
from .catalog import Catalog
from .executor import (Aggregate, CompiledPredicate, Filter, HashJoin,
                       IndexScan, NestedLoopJoin, PlanNode, Project, SeqScan)

RANGE_OPS = {"<", "<=", ">", ">="}


class Optimizer:
    def __init__(self, catalog: Catalog):
        self.cat = catalog

    # ---- column resolution ----------------------------------------------
    def _involved(self, q: ast.Select) -> List[str]:
        tables = [q.table] + [j.table for j in q.joins]
        return tables

    def _resolve_col(self, cref: ast.ColumnRef, q: ast.Select) -> str:
        if cref.table is not None:
            real = q.aliases.get(cref.table, cref.table)
            return f"{real}.{cref.name}"
        # search involved tables for a unique column match
        hits = [t for t in self._involved(q)
                if cref.name in self.cat.get(t).schema.names()]
        if len(hits) == 1:
            return f"{hits[0]}.{cref.name}"
        if not hits:
            raise KeyError(f"unknown column {cref.name}")
        raise KeyError(f"ambiguous column {cref.name} in {hits}")

    # ---- selectivity -----------------------------------------------------
    def _selectivity(self, table: str, column: str, op: str, value: Any) -> float:
        info = self.cat.get(table)
        st = info.col_stats.get(column)
        if st is None or info.n_tuples == 0:
            return 0.1 if op == "=" else 0.33
        if op == "=":
            return 1.0 / st.ndv
        if op == "<>":
            return 1.0 - 1.0 / st.ndv
        if op in RANGE_OPS and st.min is not None and st.max is not None \
                and isinstance(value, (int, float)):
            span = (st.max - st.min) or 1
            if op in ("<", "<="):
                frac = (value - st.min) / span
            else:
                frac = (st.max - value) / span
            return min(1.0, max(0.0, frac))
        return 0.33

    # ---- per-table scan choice ------------------------------------------
    def _build_scan(self, table: str, conjuncts: List[ast.Comparison],
                    q: ast.Select) -> Tuple[PlanNode, float]:
        info = self.cat.get(table)
        n = max(1, info.n_tuples)
        # single-table predicates of the form col OP literal
        local: List[Tuple[str, str, Any]] = []  # (column, op, value)
        for c in conjuncts:
            lc = isinstance(c.left, ast.ColumnRef)
            rc = isinstance(c.right, ast.ColumnRef)
            if lc and not rc:
                col = self._resolve_col(c.left, q)
                if col.startswith(table + "."):
                    local.append((col.split(".", 1)[1], c.op, c.right.value))
            elif rc and not lc:
                col = self._resolve_col(c.right, q)
                if col.startswith(table + "."):
                    local.append((col.split(".", 1)[1], _flip(c.op),
                                  c.left.value))

        combined_sel = 1.0
        for _, op, val in local:
            # selectivity uses the column name
            pass
        # estimate combined selectivity (independence)
        for col, op, val in local:
            combined_sel *= self._selectivity(table, col, op, val)
        combined_sel = min(1.0, combined_sel)

        seq_cost = float(n)
        best = (SeqScan(table, info.schema), seq_cost, "seq")

        # consider an index scan on any indexed local predicate column
        for col, op, val in local:
            if not info.has_index(col):
                continue
            sel = self._selectivity(table, col, op, val)
            matched = max(1.0, sel * n)
            idx_cost = 2 * math.log2(n + 1) + matched
            if op == "=":
                low = high = val
                desc = f"= {val!r}"
            elif op in ("<", "<="):
                low, high, desc = None, val, f"{op} {val!r}"
            elif op in (">", ">="):
                low, high, desc = val, None, f"{op} {val!r}"
            else:
                continue
            if idx_cost < best[1]:
                best = (IndexScan(table, info.schema, col, low, high, desc),
                        idx_cost, "index")

        node = best[0]
        node.est_rows = max(1.0, combined_sel * n)
        node.est_cost = best[1]

        # any local predicate not consumed by the index becomes a Filter
        pred = self._compile_local(table, local, q, used=best[2])
        if pred is not None:
            f = Filter(node, pred)
            f.est_rows = node.est_rows
            f.est_cost = node.est_cost + n
            return f, node.est_rows
        return node, node.est_rows

    def _compile_local(self, table, local, q, used) -> Optional[CompiledPredicate]:
        conj = []
        for col, op, val in local:
            key = f"{table}.{col}"
            conj.append((key, op, val, True, False))
        # If we chose an index scan it already restricts on one predicate, but
        # re-checking it in a Filter is correct and cheap; keep all for safety.
        if not conj:
            return None
        return CompiledPredicate(conj)

    # ---- join ordering ---------------------------------------------------
    def build(self, q: ast.Select) -> PlanNode:
        tables = self._involved(q)
        conjuncts = q.where.conjuncts if q.where else []

        # base scans (single-table predicates pushed down)
        plans: Dict[str, Tuple[PlanNode, float]] = {}
        for t in tables:
            plans[t] = self._build_scan(t, conjuncts, q)

        # resolve join keys
        joins = []
        for j in q.joins:
            lk = self._resolve_col(j.left, q)
            rk = self._resolve_col(j.right, q)
            joins.append((lk, rk))

        if not joins:
            root, _ = plans[q.table]
        else:
            root = self._order_joins(tables, plans, joins)

        # remaining multi-table predicates (e.g. col = col not in ON) -> Filter
        multi = []
        for c in conjuncts:
            if isinstance(c.left, ast.ColumnRef) and isinstance(c.right, ast.ColumnRef):
                lk = self._resolve_col(c.left, q)
                rk = self._resolve_col(c.right, q)
                multi.append((lk, c.op, rk, True, True))
        if multi:
            root = Filter(root, CompiledPredicate(multi))

        # projection / aggregation
        return self._finish(root, q)

    def _order_joins(self, tables, plans, joins) -> PlanNode:
        # greedy left-deep: start from smallest base plan, attach cheapest join
        joined = set()
        # pick smallest starting table
        start = min(tables, key=lambda t: plans[t][1])
        cur_node, cur_rows = plans[start]
        joined.add(start)
        cur_keys = {start}
        remaining = list(joins)
        while remaining:
            best_i = None
            best_est = None
            best = None
            for i, (lk, rk) in enumerate(remaining):
                lt, rt = lk.split(".", 1)[0], rk.split(".", 1)[0]
                # one side joined, other not
                if lt in joined and rt not in joined:
                    newt, newkey, ourkey = rt, rk, lk
                elif rt in joined and lt not in joined:
                    newt, newkey, ourkey = lt, lk, rk
                else:
                    continue
                rnode, rrows = plans[newt]
                sel = self._join_selectivity(ourkey, newkey)
                est = max(1.0, cur_rows * rrows * sel)
                if best_est is None or est < best_est:
                    best_est = est
                    best_i = i
                    best = (newt, newkey, ourkey, rnode, rrows)
            if best is None:
                # disconnected; just cross-join remaining (rare)
                i, (lk, rk) = 0, remaining[0]
                rt = rk.split(".", 1)[0] if rk.split(".", 1)[0] not in joined \
                    else lk.split(".", 1)[0]
                rnode, rrows = plans[rt]
                node = NestedLoopJoin(cur_node, rnode, lk, rk)
                node.est_rows = cur_rows * rrows
                node.est_cost = cur_rows * rrows
                cur_node, cur_rows = node, node.est_rows
                joined.add(rt)
                remaining.pop(i)
                continue
            newt, newkey, ourkey, rnode, rrows = best
            # build hash side = smaller input
            if rrows <= cur_rows:
                node = HashJoin(cur_node, rnode, ourkey, newkey)
            else:
                node = HashJoin(rnode, cur_node, newkey, ourkey)
            node.est_rows = best_est
            node.est_cost = cur_rows + rrows + best_est
            cur_node, cur_rows = node, best_est
            joined.add(newt)
            remaining.pop(best_i)
        return cur_node

    def _join_selectivity(self, lkey: str, rkey: str) -> float:
        lt, lc = lkey.split(".", 1)
        rt, rc = rkey.split(".", 1)
        ln = self.cat.get(lt).col_stats.get(lc)
        rn = self.cat.get(rt).col_stats.get(rc)
        ndv = max(ln.ndv if ln else 1, rn.ndv if rn else 1)
        return 1.0 / max(1, ndv)

    # ---- projection / aggregation ---------------------------------------
    def _finish(self, root: PlanNode, q: ast.Select) -> PlanNode:
        has_agg = any(isinstance(p, ast.Aggregate) for p in q.projections)
        if has_agg or q.group_by:
            group_keys = [self._resolve_col(g, q) for g in q.group_by]
            aggs = []
            extra_proj = []
            for p in q.projections:
                if isinstance(p, ast.Aggregate):
                    col = self._resolve_col(p.arg, q) if p.arg else None
                    label = f"{p.func}({'*' if p.arg is None else p.arg.name})"
                    aggs.append((label, p.func, col))
                    extra_proj.append((label, label))
                elif isinstance(p, ast.ColumnRef):
                    key = self._resolve_col(p, q)
                    extra_proj.append((p.name, key))
                # "*" with aggregate is ignored
            agg = Aggregate(root, aggs, group_keys)
            agg.est_rows = max(1.0, root.est_rows / 4)
            return Project(agg, extra_proj)

        # plain projection
        out_cols: List[Tuple[str, str]] = []
        for p in q.projections:
            if p == "*":
                for t in self._involved(q):
                    for cname in self.cat.get(t).schema.names():
                        out_cols.append((cname, f"{t}.{cname}"))
            elif isinstance(p, ast.ColumnRef):
                out_cols.append((p.name, self._resolve_col(p, q)))
        proj = Project(root, out_cols)
        proj.est_rows = root.est_rows
        return proj


def _flip(op: str) -> str:
    return {"<": ">", ">": "<", "<=": ">=", ">=": "<=", "=": "=", "<>": "<>"}[op]
