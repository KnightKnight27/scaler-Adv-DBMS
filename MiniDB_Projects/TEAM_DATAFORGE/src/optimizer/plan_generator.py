"""
Plan Generator — Converts parsed SQL AST into a physical execution plan.

Performs logical-to-physical plan translation, choosing between:
  - Sequential scan vs Index scan (based on cost)
  - Join ordering (exhaustive for small joins, greedy otherwise)
  - Filter pushdown

The generated plan is a tree of PlanNode objects that the executor walks.
"""

from dataclasses import dataclass, field
from typing import Optional, List
from enum import Enum, auto
from itertools import permutations

from src.catalog.catalog import Catalog
from src.parser.ast_nodes import *
from .cost_estimator import CostEstimator


class PlanNodeType(Enum):
    SEQ_SCAN = auto()
    INDEX_SCAN = auto()
    FILTER = auto()
    PROJECT = auto()
    NESTED_LOOP_JOIN = auto()
    HASH_JOIN = auto()
    SORT = auto()
    LIMIT = auto()
    INSERT = auto()
    DELETE = auto()
    UPDATE = auto()
    AGGREGATE = auto()


@dataclass
class PlanNode:
    """A node in the physical execution plan tree."""
    node_type: PlanNodeType
    table_name: Optional[str] = None
    table_alias: Optional[str] = None
    columns: List[str] = field(default_factory=list)        # For PROJECT
    predicate: Optional[Expression] = None                  # For FILTER, scan predicates
    join_condition: Optional[Expression] = None             # For JOINs
    join_type: str = 'INNER'
    children: List['PlanNode'] = field(default_factory=list)
    index_column: Optional[str] = None                      # For INDEX_SCAN
    index_key: Optional[any] = None                         # For INDEX_SCAN point lookup
    order_by: List[OrderByItem] = field(default_factory=list)
    limit_count: Optional[int] = None
    select_items: List[SelectItem] = field(default_factory=list)
    # For INSERT/DELETE
    values: Optional[list] = None
    insert_columns: Optional[list] = None
    # For AGGREGATE
    group_by: List[Expression] = field(default_factory=list)
    aggregates: List[FunctionCall] = field(default_factory=list)
    having: Optional[Expression] = None
    # Cost estimate
    estimated_cost: float = 0.0
    estimated_rows: int = 0

    def __repr__(self):
        return f"PlanNode({self.node_type.name}, table={self.table_name})"


@dataclass
class PhysicalPlan:
    """The complete physical execution plan."""
    root: PlanNode
    statement_type: str  # 'SELECT', 'INSERT', 'DELETE', 'UPDATE'
    original_ast: Statement = None


class PlanGenerator:
    """
    Generates physical execution plans from AST.

    Uses the cost estimator to choose between table scan and index scan,
    and to determine optimal join ordering.

    Usage:
        gen = PlanGenerator(catalog, cost_estimator)
        plan = gen.generate(select_ast)
    """

    def __init__(self, catalog: Catalog, cost_estimator: CostEstimator):
        self.catalog = catalog
        self.cost_estimator = cost_estimator

    def generate(self, stmt: Statement) -> PhysicalPlan:
        """
        Generate a physical plan from a parsed statement.

        Args:
            stmt: Parsed AST statement.

        Returns:
            A PhysicalPlan.
        """
        if isinstance(stmt, SelectStatement):
            return self._plan_select(stmt)
        elif isinstance(stmt, InsertStatement):
            return self._plan_insert(stmt)
        elif isinstance(stmt, DeleteStatement):
            return self._plan_delete(stmt)
        elif isinstance(stmt, UpdateStatement):
            return self._plan_update(stmt)
        else:
            raise ValueError(f"Cannot generate plan for: {type(stmt).__name__}")

    # ─── SELECT Plan ─────────────────────────────────────────────────

    def _plan_select(self, stmt: SelectStatement) -> PhysicalPlan:
        """Generate plan for SELECT statement."""
        # Start with the base table scan
        if stmt.from_table is None:
            # SELECT without FROM (e.g., SELECT 1+1)
            root = PlanNode(node_type=PlanNodeType.PROJECT, select_items=stmt.columns)
            return PhysicalPlan(root=root, statement_type='SELECT', original_ast=stmt)

        # Base table scan
        root = self._plan_table_scan(stmt.from_table, stmt.where if not stmt.joins else None)

        # Handle JOINs
        if stmt.joins:
            root = self._plan_joins(root, stmt.from_table, stmt.joins, stmt.where)
        elif stmt.where and root.node_type != PlanNodeType.FILTER:
            # Apply WHERE filter if not already applied by index scan
            if root.predicate is None:
                filter_node = PlanNode(
                    node_type=PlanNodeType.FILTER,
                    predicate=stmt.where,
                    children=[root],
                )
                root = filter_node

        # GROUP BY / Aggregates
        has_aggregates = any(
            isinstance(item.expr, FunctionCall) for item in stmt.columns
        )
        if stmt.group_by or has_aggregates:
            agg_funcs = [item.expr for item in stmt.columns if isinstance(item.expr, FunctionCall)]
            agg_node = PlanNode(
                node_type=PlanNodeType.AGGREGATE,
                group_by=stmt.group_by,
                aggregates=agg_funcs,
                having=stmt.having,
                select_items=stmt.columns,
                children=[root],
            )
            root = agg_node

        # ORDER BY
        if stmt.order_by:
            sort_node = PlanNode(
                node_type=PlanNodeType.SORT,
                order_by=stmt.order_by,
                children=[root],
            )
            root = sort_node

        # LIMIT
        if stmt.limit is not None:
            limit_node = PlanNode(
                node_type=PlanNodeType.LIMIT,
                limit_count=stmt.limit,
                children=[root],
            )
            root = limit_node

        # PROJECT (column selection)
        project_node = PlanNode(
            node_type=PlanNodeType.PROJECT,
            select_items=stmt.columns,
            children=[root],
        )
        root = project_node

        return PhysicalPlan(root=root, statement_type='SELECT', original_ast=stmt)

    def _plan_table_scan(self, table_ref: TableRef,
                          where_clause: Expression = None) -> PlanNode:
        """
        Choose between sequential scan and index scan for a table.

        Uses cost estimation to make the decision.
        """
        table_name = table_ref.table_name
        table_alias = table_ref.alias or table_name
        table_info = self.catalog.get_table(table_name)

        # Try index scan if we have a suitable WHERE clause
        if where_clause and table_info:
            index_col, index_key = self._extract_index_predicate(where_clause, table_info)
            if index_col:
                selectivity = self.cost_estimator.estimate_selectivity(
                    table_name, where_clause, table_alias
                )
                if self.cost_estimator.should_use_index(table_name, index_col, selectivity):
                    return PlanNode(
                        node_type=PlanNodeType.INDEX_SCAN,
                        table_name=table_name,
                        table_alias=table_alias,
                        index_column=index_col,
                        index_key=index_key,
                        predicate=where_clause,
                        estimated_cost=self.cost_estimator.estimate_index_scan_cost(
                            table_name, selectivity
                        ),
                    )

        # Default to sequential scan
        return PlanNode(
            node_type=PlanNodeType.SEQ_SCAN,
            table_name=table_name,
            table_alias=table_alias,
            estimated_cost=self.cost_estimator.estimate_seq_scan_cost(table_name),
        )

    def _extract_index_predicate(self, expr: Expression,
                                  table_info) -> tuple:
        """
        Extract a column and value suitable for index lookup.

        Returns (column_name, lookup_value) if found, else (None, None).
        """
        if isinstance(expr, BinaryOp) and expr.op in ('=', '=='):
            # Check if one side is a column ref with an index
            if isinstance(expr.left, ColumnRef) and isinstance(expr.right, Literal):
                col = expr.left.column
                if table_info.has_index(col):
                    return (col, expr.right.value)
            if isinstance(expr.right, ColumnRef) and isinstance(expr.left, Literal):
                col = expr.right.column
                if table_info.has_index(col):
                    return (col, expr.left.value)

        # Check AND for index-applicable subexpressions
        if isinstance(expr, BinaryOp) and expr.op.upper() == 'AND':
            result = self._extract_index_predicate(expr.left, table_info)
            if result[0]:
                return result
            return self._extract_index_predicate(expr.right, table_info)

        return (None, None)

    def _plan_joins(self, base_scan: PlanNode, base_table: TableRef,
                     joins: List[JoinClause], where: Expression) -> PlanNode:
        """
        Plan JOIN operations with join ordering optimization.

        For 2-3 tables, tries all permutations. For more, uses left-deep ordering.
        """
        # Collect all tables
        tables = [(base_table, base_scan)]
        for join in joins:
            scan = self._plan_table_scan(join.table)
            tables.append((join.table, scan))

        if len(tables) <= 3:
            # Try all join orderings
            return self._optimize_join_order(tables, joins, where)
        else:
            # Left-deep plan in given order
            return self._left_deep_join(base_scan, joins, where)

    def _optimize_join_order(self, tables, joins, where) -> PlanNode:
        """Try all join orderings and pick the cheapest."""
        best_plan = None
        best_cost = float('inf')

        # For simplicity with 2 tables, just do left-deep
        if len(tables) == 2:
            left_ref, left_scan = tables[0]
            right_ref, right_scan = tables[1]
            join_cond = joins[0].condition if joins else None

            plan = PlanNode(
                node_type=PlanNodeType.NESTED_LOOP_JOIN,
                join_type=joins[0].join_type if joins else 'INNER',
                join_condition=join_cond,
                children=[left_scan, right_scan],
            )

            # Apply remaining WHERE predicate
            if where:
                plan = PlanNode(
                    node_type=PlanNodeType.FILTER,
                    predicate=where,
                    children=[plan],
                )

            return plan

        # For 3 tables, try permutations of join order
        base_ref, base_scan = tables[0]
        for perm in permutations(range(1, len(tables))):
            current = base_scan
            cost = 0

            for idx in perm:
                join_ref, join_scan = tables[idx]
                join_idx = idx - 1
                join_clause = joins[join_idx] if join_idx < len(joins) else None
                join_cond = join_clause.condition if join_clause else None

                current = PlanNode(
                    node_type=PlanNodeType.NESTED_LOOP_JOIN,
                    join_type=join_clause.join_type if join_clause else 'INNER',
                    join_condition=join_cond,
                    children=[current, join_scan],
                )

                # Estimate cost
                outer_table = base_ref.table_name
                inner_table = join_ref.table_name
                cost += self.cost_estimator.estimate_nested_loop_join_cost(
                    outer_table, inner_table
                )

            if cost < best_cost:
                best_cost = cost
                best_plan = current

        # Apply WHERE filter
        if where and best_plan:
            best_plan = PlanNode(
                node_type=PlanNodeType.FILTER,
                predicate=where,
                children=[best_plan],
            )

        return best_plan

    def _left_deep_join(self, base_scan, joins, where) -> PlanNode:
        """Build a left-deep join tree."""
        current = base_scan
        for join in joins:
            right_scan = self._plan_table_scan(join.table)
            current = PlanNode(
                node_type=PlanNodeType.NESTED_LOOP_JOIN,
                join_type=join.join_type,
                join_condition=join.condition,
                children=[current, right_scan],
            )

        if where:
            current = PlanNode(
                node_type=PlanNodeType.FILTER,
                predicate=where,
                children=[current],
            )

        return current

    # ─── INSERT Plan ─────────────────────────────────────────────────

    def _plan_insert(self, stmt: InsertStatement) -> PhysicalPlan:
        node = PlanNode(
            node_type=PlanNodeType.INSERT,
            table_name=stmt.table_name,
            values=stmt.values,
            insert_columns=stmt.columns,
        )
        return PhysicalPlan(root=node, statement_type='INSERT', original_ast=stmt)

    # ─── DELETE Plan ─────────────────────────────────────────────────

    def _plan_delete(self, stmt: DeleteStatement) -> PhysicalPlan:
        # Scan for rows to delete
        table_ref = TableRef(table_name=stmt.table_name)
        scan = self._plan_table_scan(table_ref, stmt.where)

        if stmt.where and scan.node_type != PlanNodeType.INDEX_SCAN:
            scan = PlanNode(
                node_type=PlanNodeType.FILTER,
                predicate=stmt.where,
                children=[scan],
            )

        node = PlanNode(
            node_type=PlanNodeType.DELETE,
            table_name=stmt.table_name,
            children=[scan],
        )
        return PhysicalPlan(root=node, statement_type='DELETE', original_ast=stmt)

    # ─── UPDATE Plan ─────────────────────────────────────────────────

    def _plan_update(self, stmt: UpdateStatement) -> PhysicalPlan:
        table_ref = TableRef(table_name=stmt.table_name)
        scan = self._plan_table_scan(table_ref, stmt.where)

        if stmt.where and scan.node_type != PlanNodeType.INDEX_SCAN:
            scan = PlanNode(
                node_type=PlanNodeType.FILTER,
                predicate=stmt.where,
                children=[scan],
            )

        node = PlanNode(
            node_type=PlanNodeType.UPDATE,
            table_name=stmt.table_name,
            children=[scan],
        )
        return PhysicalPlan(root=node, statement_type='UPDATE', original_ast=stmt)

    # ─── Plan Explanation ────────────────────────────────────────────

    @staticmethod
    def explain(plan: PhysicalPlan) -> str:
        """Generate a human-readable explanation of the plan."""
        lines = []
        PlanGenerator._explain_node(plan.root, lines, indent=0)
        return '\n'.join(lines)

    @staticmethod
    def _explain_node(node: PlanNode, lines: list, indent: int):
        prefix = '  ' * indent + '→ '

        if node.node_type == PlanNodeType.SEQ_SCAN:
            lines.append(f"{prefix}SeqScan on {node.table_name}"
                         f" (alias={node.table_alias}, cost={node.estimated_cost:.1f})")
        elif node.node_type == PlanNodeType.INDEX_SCAN:
            lines.append(f"{prefix}IndexScan on {node.table_name}.{node.index_column}"
                         f" key={node.index_key} (cost={node.estimated_cost:.1f})")
        elif node.node_type == PlanNodeType.FILTER:
            lines.append(f"{prefix}Filter")
        elif node.node_type == PlanNodeType.PROJECT:
            cols = [str(s.expr) if hasattr(s, 'expr') else str(s) for s in node.select_items]
            lines.append(f"{prefix}Project [{', '.join(cols[:5])}]")
        elif node.node_type == PlanNodeType.NESTED_LOOP_JOIN:
            lines.append(f"{prefix}NestedLoopJoin ({node.join_type})")
        elif node.node_type == PlanNodeType.SORT:
            lines.append(f"{prefix}Sort")
        elif node.node_type == PlanNodeType.LIMIT:
            lines.append(f"{prefix}Limit {node.limit_count}")
        elif node.node_type == PlanNodeType.INSERT:
            lines.append(f"{prefix}Insert into {node.table_name}")
        elif node.node_type == PlanNodeType.DELETE:
            lines.append(f"{prefix}Delete from {node.table_name}")
        elif node.node_type == PlanNodeType.UPDATE:
            lines.append(f"{prefix}Update {node.table_name}")
        elif node.node_type == PlanNodeType.AGGREGATE:
            lines.append(f"{prefix}Aggregate")
        else:
            lines.append(f"{prefix}{node.node_type.name}")

        for child in node.children:
            PlanGenerator._explain_node(child, lines, indent + 1)
