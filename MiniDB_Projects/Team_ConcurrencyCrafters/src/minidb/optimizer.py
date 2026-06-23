from __future__ import annotations

import math

from .catalog import Catalog
from .parser import SelectStatement
from .types import Predicate, QueryPlan, TableStats


class QueryOptimizer:
    def __init__(self, catalog: Catalog):
        self.catalog = catalog

    def optimize_select(self, statement: SelectStatement) -> QueryPlan:
        if statement.join is not None:
            plan = self._optimize_join(statement)
        else:
            plan = self._optimize_single_table(statement)
        if statement.count_star:
            plan.details["aggregate"] = "COUNT(*)"
        return plan

    def _optimize_single_table(self, statement: SelectStatement) -> QueryPlan:
        metadata = self.catalog.get_table(statement.table_name)
        stats = metadata.stats or TableStats(row_count=0, page_count=0)
        row_count = max(stats.row_count, 1)
        predicates = self._statement_predicates(statement)

        range_column, lower_bound, upper_bound = self._indexed_range_bounds(
            statement.table_name,
            predicates,
        )
        if range_column is not None:
            index = self.catalog.get_index_for_column(statement.table_name, range_column)
            assert index is not None
            estimated_rows = max(1.0, row_count * 0.25)
            return QueryPlan(
                operator="INDEX_RANGE_SCAN",
                table=statement.table_name,
                predicate=predicates[0] if predicates else None,
                estimated_rows=estimated_rows,
                estimated_cost=math.log2(row_count + 1) + estimated_rows,
                reason=f"Indexed range predicate on {range_column}.",
                details={
                    "access": "B+Tree Range",
                    "index": index.name,
                    "lower_bound": lower_bound,
                    "upper_bound": upper_bound,
                },
            )

        indexed_predicate = self._indexed_equality_predicate(statement.table_name, predicates)
        if indexed_predicate is not None:
            index = self.catalog.get_index_for_column(
                statement.table_name,
                indexed_predicate.column,
            )
            if index is not None:
                estimated_rows = max(
                    1.0,
                    row_count
                    * self._estimate_selectivity(
                        statement.table_name,
                        indexed_predicate.column,
                        indexed=True,
                    ),
                )
                return QueryPlan(
                    operator="INDEX_SCAN",
                    table=statement.table_name,
                    predicate=indexed_predicate,
                    estimated_rows=estimated_rows,
                    estimated_cost=math.log2(row_count + 1) + 1.0,
                    reason=f"Indexed equality predicate on {indexed_predicate.column}.",
                    details={
                        "index": index.name,
                        "access": "B+Tree",
                        "filters": len(predicates),
                    },
                )
        estimated_rows = max(
            1.0 if stats.row_count else 0.0,
            row_count * self._estimate_selectivity(
                statement.table_name,
                predicates[0].column if predicates else None,
                indexed=False,
            ),
        )
        return QueryPlan(
            operator="TABLE_SCAN",
            table=statement.table_name,
            predicate=predicates[0] if predicates else None,
            estimated_rows=estimated_rows,
            estimated_cost=max(float(stats.page_count), 1.0 if stats.row_count else 0.0),
            reason="No matching index, so the optimizer chose a heap table scan.",
            details={"pages": stats.page_count, "filters": len(predicates)},
        )

    def _optimize_join(self, statement: SelectStatement) -> QueryPlan:
        assert statement.join is not None
        left_stats = self.catalog.get_table(statement.join.left_table).stats or TableStats(row_count=0, page_count=0)
        right_stats = self.catalog.get_table(statement.join.right_table).stats or TableStats(row_count=0, page_count=0)
        left_rows = max(left_stats.row_count, 1)
        right_rows = max(right_stats.row_count, 1)
        if left_rows <= right_rows:
            outer_table, inner_table = statement.join.left_table, statement.join.right_table
            outer_rows, inner_rows = left_rows, right_rows
        else:
            outer_table, inner_table = statement.join.right_table, statement.join.left_table
            outer_rows, inner_rows = right_rows, left_rows
        child_outer = QueryPlan(
            operator="TABLE_SCAN",
            table=outer_table,
            estimated_rows=float(outer_rows),
            estimated_cost=float(self.catalog.get_table(outer_table).stats.page_count),
            reason="Outer relation chosen first because it has the smaller estimated cardinality.",
            details={"pages": self.catalog.get_table(outer_table).stats.page_count},
        )
        child_inner = QueryPlan(
            operator="TABLE_SCAN",
            table=inner_table,
            estimated_rows=float(inner_rows),
            estimated_cost=float(self.catalog.get_table(inner_table).stats.page_count),
            reason="Inner relation scanned second for the nested-loop join.",
            details={"pages": self.catalog.get_table(inner_table).stats.page_count},
        )
        return QueryPlan(
            operator="NESTED_LOOP_JOIN",
            join=statement.join,
            estimated_rows=float(min(left_rows, right_rows)),
            estimated_cost=float(outer_rows * inner_rows) + child_outer.estimated_cost + child_inner.estimated_cost,
            reason=f"Join order selected with smaller input first: {outer_table} before {inner_table}.",
            details={
                "outer": outer_table,
                "inner": inner_table,
                "join_on": f"{statement.join.left_table}.{statement.join.left_column}={statement.join.right_table}.{statement.join.right_column}",
            },
            children=[child_outer, child_inner],
        )

    def _estimate_selectivity(
        self, table_name: str, column_name: str | None, *, indexed: bool
    ) -> float:
        stats = self.catalog.get_table(table_name).stats or TableStats(row_count=0, page_count=0)
        if column_name is None:
            return 1.0
        table = self.catalog.get_table(table_name)
        primary_key = table.schema.primary_key
        if indexed and primary_key is not None and primary_key.name == column_name:
            return 1.0 / max(stats.row_count, 1)
        return 0.1

    @staticmethod
    def _statement_predicates(statement: SelectStatement) -> list[Predicate]:
        if statement.predicates:
            return list(statement.predicates)
        return [statement.predicate] if statement.predicate is not None else []

    def _indexed_equality_predicate(
        self, table_name: str, predicates: list[Predicate]
    ) -> Predicate | None:
        for predicate in predicates:
            if predicate.operator != "=":
                continue
            if self.catalog.get_index_for_column(table_name, predicate.column) is not None:
                return predicate
        return None

    def _indexed_range_bounds(
        self, table_name: str, predicates: list[Predicate]
    ) -> tuple[str | None, int | None, int | None]:
        bounds: dict[str, dict[str, int]] = {}
        for predicate in predicates:
            if predicate.operator not in {">=", "<=", ">", "<"}:
                continue
            if not isinstance(predicate.value, int):
                continue
            if self.catalog.get_index_for_column(table_name, predicate.column) is None:
                continue
            current = bounds.setdefault(predicate.column, {})
            value = int(predicate.value)
            if predicate.operator == ">=":
                current["lower"] = max(current.get("lower", value), value)
            elif predicate.operator == ">":
                current["lower"] = max(current.get("lower", value + 1), value + 1)
            elif predicate.operator == "<=":
                current["upper"] = min(current.get("upper", value), value)
            elif predicate.operator == "<":
                current["upper"] = min(current.get("upper", value - 1), value - 1)
        for column, column_bounds in bounds.items():
            lower = column_bounds.get("lower")
            upper = column_bounds.get("upper")
            if lower is not None or upper is not None:
                return column, lower, upper
        return None, None, None

