"""
AST Nodes — Abstract Syntax Tree node definitions for MiniDB SQL.

Each SQL statement and expression is represented as an AST node.
The parser produces these nodes; the optimizer/executor consumes them.
"""

from dataclasses import dataclass, field
from typing import Optional, List, Any


# ─── Expressions ──────────────────────────────────────────────────────────────

@dataclass
class Expression:
    """Base class for all expressions."""
    pass


@dataclass
class Literal(Expression):
    """A literal value (number, string, boolean, NULL)."""
    value: Any
    data_type: str = 'UNKNOWN'  # 'INTEGER', 'FLOAT', 'STRING', 'BOOLEAN', 'NULL'


@dataclass
class ColumnRef(Expression):
    """Reference to a column, optionally qualified with table name."""
    column: str
    table: Optional[str] = None

    def full_name(self) -> str:
        if self.table:
            return f"{self.table}.{self.column}"
        return self.column


@dataclass
class BinaryOp(Expression):
    """Binary operation: left <op> right."""
    left: Expression
    op: str              # '=', '!=', '<', '>', '<=', '>=', 'AND', 'OR', '+', '-', '*', '/'
    right: Expression


@dataclass
class UnaryOp(Expression):
    """Unary operation: NOT expr, -expr."""
    op: str
    operand: Expression


@dataclass
class IsNullExpr(Expression):
    """IS NULL / IS NOT NULL expression."""
    expr: Expression
    negated: bool = False  # True for IS NOT NULL


@dataclass
class BetweenExpr(Expression):
    """BETWEEN low AND high expression."""
    expr: Expression
    low: Expression
    high: Expression


@dataclass
class InExpr(Expression):
    """IN (val1, val2, ...) expression."""
    expr: Expression
    values: List[Expression] = field(default_factory=list)


@dataclass
class FunctionCall(Expression):
    """Aggregate function call: COUNT(*), SUM(col), AVG(col), etc."""
    name: str
    args: List[Expression] = field(default_factory=list)
    distinct: bool = False


@dataclass
class StarExpr(Expression):
    """SELECT * — all columns."""
    table: Optional[str] = None


# ─── SELECT components ────────────────────────────────────────────────────────

@dataclass
class SelectItem:
    """A single item in SELECT list."""
    expr: Expression
    alias: Optional[str] = None


@dataclass
class TableRef:
    """A table reference in FROM clause."""
    table_name: str
    alias: Optional[str] = None


@dataclass
class JoinClause:
    """A JOIN clause."""
    join_type: str          # 'INNER', 'LEFT', 'RIGHT', 'CROSS'
    table: TableRef
    condition: Optional[Expression] = None  # ON condition


@dataclass
class OrderByItem:
    """An item in ORDER BY clause."""
    expr: Expression
    ascending: bool = True


# ─── Statements ───────────────────────────────────────────────────────────────

@dataclass
class Statement:
    """Base class for all SQL statements."""
    pass


@dataclass
class SelectStatement(Statement):
    """SELECT statement."""
    columns: List[SelectItem] = field(default_factory=list)
    from_table: Optional[TableRef] = None
    joins: List[JoinClause] = field(default_factory=list)
    where: Optional[Expression] = None
    group_by: List[Expression] = field(default_factory=list)
    having: Optional[Expression] = None
    order_by: List[OrderByItem] = field(default_factory=list)
    limit: Optional[int] = None
    distinct: bool = False


@dataclass
class InsertStatement(Statement):
    """INSERT INTO table (cols) VALUES (vals)."""
    table_name: str = ''
    columns: List[str] = field(default_factory=list)
    values: List[List[Expression]] = field(default_factory=list)  # Multiple rows


@dataclass
class DeleteStatement(Statement):
    """DELETE FROM table WHERE condition."""
    table_name: str = ''
    where: Optional[Expression] = None


@dataclass
class UpdateStatement(Statement):
    """UPDATE table SET col=val, ... WHERE condition."""
    table_name: str = ''
    assignments: List[tuple] = field(default_factory=list)  # [(column, expression), ...]
    where: Optional[Expression] = None


@dataclass
class CreateTableStatement(Statement):
    """CREATE TABLE name (columns...)."""
    table_name: str = ''
    columns: List[dict] = field(default_factory=list)
    # Each column: {'name': str, 'type': str, 'primary_key': bool, 'nullable': bool}


@dataclass
class DropTableStatement(Statement):
    """DROP TABLE name."""
    table_name: str = ''


@dataclass
class CreateIndexStatement(Statement):
    """CREATE INDEX name ON table (column)."""
    index_name: str = ''
    table_name: str = ''
    column_name: str = ''


@dataclass
class BeginStatement(Statement):
    """BEGIN TRANSACTION."""
    pass


@dataclass
class CommitStatement(Statement):
    """COMMIT."""
    pass


@dataclass
class RollbackStatement(Statement):
    """ROLLBACK."""
    pass
