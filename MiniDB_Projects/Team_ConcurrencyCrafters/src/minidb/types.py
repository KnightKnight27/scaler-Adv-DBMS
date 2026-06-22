from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum
from typing import Any

Value = int | str | None
TransactionID = int
ResourceID = str


class TransactionMode(str, Enum):
    TWO_PL = "2PL"
    MVCC = "MVCC"


class TransactionState(str, Enum):
    ACTIVE = "ACTIVE"
    COMMITTED = "COMMITTED"
    ABORTED = "ABORTED"


class LockType(str, Enum):
    SHARED = "SHARED"
    EXCLUSIVE = "EXCLUSIVE"


@dataclass(slots=True)
class RecordID:
    page_id: int
    slot_id: int


@dataclass(slots=True)
class Record:
    rid: RecordID | None
    values: dict[str, Value]
    deleted: bool = False


@dataclass(slots=True)
class Column:
    name: str
    data_type: str
    primary_key: bool = False
    indexed: bool = False

    def normalized_type(self) -> str:
        return self.data_type.upper()


@dataclass(slots=True)
class TableSchema:
    name: str
    columns: list[Column]

    def get_column(self, column_name: str) -> Column:
        for column in self.columns:
            if column.name == column_name:
                return column
        raise KeyError(f"Unknown column '{column_name}' in table '{self.name}'.")

    @property
    def primary_key(self) -> Column | None:
        for column in self.columns:
            if column.primary_key:
                return column
        return None

    @property
    def column_names(self) -> list[str]:
        return [column.name for column in self.columns]


@dataclass(slots=True)
class Predicate:
    column: str
    operator: str
    value: Value


@dataclass(slots=True)
class JoinClause:
    left_table: str
    right_table: str
    left_column: str
    right_column: str


@dataclass(slots=True)
class QueryPlan:
    operator: str
    table: str | None = None
    predicate: Predicate | None = None
    join: JoinClause | None = None
    estimated_rows: float = 0.0
    estimated_cost: float = 0.0
    reason: str = ""
    details: dict[str, Any] = field(default_factory=dict)
    children: list["QueryPlan"] = field(default_factory=list)

    def explain(self, depth: int = 0) -> str:
        indent = "  " * depth
        header = (
            f"{indent}{self.operator}"
            f" rows={self.estimated_rows:.2f}"
            f" cost={self.estimated_cost:.2f}"
        )
        parts = [header]
        if self.table:
            parts.append(f"{indent}table={self.table}")
        if self.reason:
            parts.append(f"{indent}reason={self.reason}")
        if self.details:
            details_text = ", ".join(
                f"{key}={value}" for key, value in sorted(self.details.items())
            )
            parts.append(f"{indent}details={details_text}")
        for child in self.children:
            parts.append(child.explain(depth + 1))
        return "\n".join(parts)


@dataclass(slots=True)
class TableStats:
    row_count: int = 0
    page_count: int = 0


class StorageEngine(ABC):
    @abstractmethod
    def create_table(self, schema: TableSchema) -> None:
        raise NotImplementedError

    @abstractmethod
    def create_index(self, table_name: str, index_name: str, column_name: str) -> None:
        raise NotImplementedError

    @abstractmethod
    def insert(self, table_name: str, row: dict[str, Value]) -> RecordID:
        raise NotImplementedError

    @abstractmethod
    def read(self, table_name: str, rid: RecordID) -> dict[str, Value] | None:
        raise NotImplementedError

    @abstractmethod
    def delete(self, table_name: str, rid: RecordID) -> dict[str, Value] | None:
        raise NotImplementedError

    @abstractmethod
    def restore(self, table_name: str, rid: RecordID, row: dict[str, Value]) -> None:
        raise NotImplementedError

    @abstractmethod
    def scan(self, table_name: str) -> list[tuple[RecordID, dict[str, Value]]]:
        raise NotImplementedError

    @abstractmethod
    def scan_record_ids(self, table_name: str) -> list[RecordID]:
        raise NotImplementedError

    @abstractmethod
    def lookup_index(
        self, table_name: str, column_name: str, key: int
    ) -> list[RecordID]:
        raise NotImplementedError

    @abstractmethod
    def get_stats(self, table_name: str) -> TableStats:
        raise NotImplementedError

