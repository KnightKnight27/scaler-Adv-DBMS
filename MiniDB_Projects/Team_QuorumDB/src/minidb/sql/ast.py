"""Abstract syntax tree for MiniDB's SQL subset.

The parser turns SQL text into these dataclasses, which the planner then turns
into an execution plan. The supported surface is intentionally small but
complete enough to demonstrate end-to-end query processing:

    CREATE TABLE / CREATE INDEX / DROP TABLE
    INSERT INTO ... VALUES ...
    DELETE FROM ... [WHERE ...]
    SELECT ... FROM ... [JOIN ... ON ...] [WHERE ...]
    BEGIN / COMMIT / ROLLBACK

Predicates are conjunctions (AND) of simple comparisons; each comparison has a
column on the left and either a literal or another column (for joins) on the
right.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, List, Optional


# -- expressions -----------------------------------------------------------
@dataclass(frozen=True)
class ColumnRef:
    name: str
    table: Optional[str] = None      # qualifier, e.g. the "u" in u.id

    @property
    def qualified(self) -> str:
        return f"{self.table}.{self.name}" if self.table else self.name


@dataclass(frozen=True)
class Literal:
    value: Any                       # int | float | str | bool | None


@dataclass(frozen=True)
class Comparison:
    left: ColumnRef
    op: str                          # one of = != < <= > >=
    right: Any                       # Literal or ColumnRef

    @property
    def is_join_predicate(self) -> bool:
        return isinstance(self.right, ColumnRef)


@dataclass
class Predicate:
    """A conjunction (AND) of comparisons. Empty means 'always true'."""
    comparisons: List[Comparison] = field(default_factory=list)

    def __bool__(self) -> bool:
        return bool(self.comparisons)


# -- statements ------------------------------------------------------------
@dataclass
class ColumnDef:
    name: str
    type: str
    not_null: bool = False
    primary_key: bool = False


@dataclass
class CreateTable:
    name: str
    columns: List[ColumnDef]
    pk_column: Optional[str] = None


@dataclass
class CreateIndex:
    table: str
    column: str
    unique: bool = False
    name: Optional[str] = None


@dataclass
class DropTable:
    name: str


@dataclass
class Insert:
    table: str
    columns: Optional[List[str]]     # None means "all columns, in order"
    rows: List[List[Any]]


@dataclass
class Delete:
    table: str
    where: Predicate = field(default_factory=Predicate)


@dataclass
class JoinClause:
    table: str
    on: Comparison
    alias: Optional[str] = None

    @property
    def qualifier(self) -> str:
        return self.alias or self.table


@dataclass
class Select:
    columns: List[str]               # ["*"] or explicit (possibly qualified)
    from_table: str
    from_alias: Optional[str] = None
    joins: List[JoinClause] = field(default_factory=list)
    where: Predicate = field(default_factory=Predicate)

    @property
    def from_qualifier(self) -> str:
        return self.from_alias or self.from_table


@dataclass
class Begin:
    pass


@dataclass
class Commit:
    pass


@dataclass
class Rollback:
    pass
