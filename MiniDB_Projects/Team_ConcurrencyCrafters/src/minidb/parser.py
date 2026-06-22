from __future__ import annotations

import re
from dataclasses import dataclass, field

from .types import Column, JoinClause, Predicate, TransactionMode, Value


class SQLParseError(ValueError):
    pass


@dataclass(slots=True)
class Statement:
    raw_sql: str


@dataclass(slots=True)
class CreateTableStatement(Statement):
    table_name: str
    columns: list[Column]


@dataclass(slots=True)
class CreateIndexStatement(Statement):
    index_name: str
    table_name: str
    column_name: str


@dataclass(slots=True)
class InsertStatement(Statement):
    table_name: str
    values: list[Value]


@dataclass(slots=True)
class SelectStatement(Statement):
    table_name: str
    predicate: Predicate | None = None
    join: JoinClause | None = None
    explain: bool = False
    count_star: bool = False


@dataclass(slots=True)
class DeleteStatement(Statement):
    table_name: str
    predicate: Predicate


@dataclass(slots=True)
class BeginStatement(Statement):
    pass


@dataclass(slots=True)
class CommitStatement(Statement):
    pass


@dataclass(slots=True)
class RollbackStatement(Statement):
    pass


@dataclass(slots=True)
class SetModeStatement(Statement):
    mode: TransactionMode


class SQLParser:
    CREATE_TABLE_RE = re.compile(
        r"^CREATE\s+TABLE\s+(?P<table>\w+)\s*\((?P<columns>.+)\)$",
        re.IGNORECASE | re.DOTALL,
    )
    CREATE_INDEX_RE = re.compile(
        r"^CREATE\s+INDEX\s+(?P<index>\w+)\s+ON\s+(?P<table>\w+)\((?P<column>\w+)\)$",
        re.IGNORECASE,
    )
    INSERT_RE = re.compile(
        r"^INSERT\s+INTO\s+(?P<table>\w+)\s+VALUES\s*\((?P<values>.+)\)$",
        re.IGNORECASE | re.DOTALL,
    )
    JOIN_SELECT_RE = re.compile(
        r"^SELECT\s+(?P<select>\*|COUNT\(\*\))\s+FROM\s+(?P<left_table>\w+)\s+JOIN\s+(?P<right_table>\w+)\s+ON\s+(?P<left_expr>\w+\.\w+)\s*=\s*(?P<right_expr>\w+\.\w+)$",
        re.IGNORECASE,
    )
    SIMPLE_SELECT_RE = re.compile(
        r"^SELECT\s+(?P<select>\*|COUNT\(\*\))\s+FROM\s+(?P<table>\w+)(?:\s+WHERE\s+(?P<column>\w+)\s*=\s*(?P<value>.+))?$",
        re.IGNORECASE | re.DOTALL,
    )
    DELETE_RE = re.compile(
        r"^DELETE\s+FROM\s+(?P<table>\w+)\s+WHERE\s+(?P<column>\w+)\s*=\s*(?P<value>.+)$",
        re.IGNORECASE | re.DOTALL,
    )
    EXPLAIN_RE = re.compile(r"^EXPLAIN\s+(.+)$", re.IGNORECASE | re.DOTALL)
    SET_MODE_RE = re.compile(r"^SET\s+MODE\s+(2PL|MVCC)$", re.IGNORECASE)

    def parse(self, sql: str) -> Statement:
        cleaned = sql.strip().rstrip(";").strip()
        if not cleaned:
            raise SQLParseError("Empty SQL statement.")

        explain_match = self.EXPLAIN_RE.match(cleaned)
        if explain_match:
            inner = self.parse(explain_match.group(1))
            if not isinstance(inner, SelectStatement):
                raise SQLParseError("EXPLAIN currently supports SELECT statements only.")
            inner.explain = True
            return inner

        upper = cleaned.upper()
        if upper == "BEGIN":
            return BeginStatement(raw_sql=sql)
        if upper == "COMMIT":
            return CommitStatement(raw_sql=sql)
        if upper == "ROLLBACK":
            return RollbackStatement(raw_sql=sql)

        if mode_match := self.SET_MODE_RE.match(cleaned):
            mode = TransactionMode.TWO_PL if mode_match.group(1).upper() == "2PL" else TransactionMode.MVCC
            return SetModeStatement(raw_sql=sql, mode=mode)

        if create_table_match := self.CREATE_TABLE_RE.match(cleaned):
            table_name = create_table_match.group("table")
            columns = [self._parse_column(definition) for definition in self._split_csv(create_table_match.group("columns"))]
            return CreateTableStatement(raw_sql=sql, table_name=table_name, columns=columns)

        if create_index_match := self.CREATE_INDEX_RE.match(cleaned):
            return CreateIndexStatement(
                raw_sql=sql,
                index_name=create_index_match.group("index"),
                table_name=create_index_match.group("table"),
                column_name=create_index_match.group("column"),
            )

        if insert_match := self.INSERT_RE.match(cleaned):
            values = [self._parse_value(token) for token in self._split_csv(insert_match.group("values"))]
            return InsertStatement(
                raw_sql=sql,
                table_name=insert_match.group("table"),
                values=values,
            )

        if join_match := self.JOIN_SELECT_RE.match(cleaned):
            left_table = join_match.group("left_table")
            right_table = join_match.group("right_table")
            left_table_name, left_column = join_match.group("left_expr").split(".", 1)
            right_table_name, right_column = join_match.group("right_expr").split(".", 1)
            return SelectStatement(
                raw_sql=sql,
                table_name=left_table,
                join=JoinClause(
                    left_table=left_table_name,
                    right_table=right_table_name,
                    left_column=left_column,
                    right_column=right_column,
                ),
                count_star=join_match.group("select").upper() == "COUNT(*)",
            )

        if select_match := self.SIMPLE_SELECT_RE.match(cleaned):
            predicate = None
            if select_match.group("column"):
                predicate = Predicate(
                    column=select_match.group("column"),
                    operator="=",
                    value=self._parse_value(select_match.group("value")),
                )
            return SelectStatement(
                raw_sql=sql,
                table_name=select_match.group("table"),
                predicate=predicate,
                count_star=select_match.group("select").upper() == "COUNT(*)",
            )

        if delete_match := self.DELETE_RE.match(cleaned):
            predicate = Predicate(
                column=delete_match.group("column"),
                operator="=",
                value=self._parse_value(delete_match.group("value")),
            )
            return DeleteStatement(
                raw_sql=sql,
                table_name=delete_match.group("table"),
                predicate=predicate,
            )

        raise SQLParseError(f"Unsupported SQL: {sql}")

    def _parse_column(self, definition: str) -> Column:
        parts = [part for part in definition.strip().split() if part]
        if len(parts) < 2:
            raise SQLParseError(f"Invalid column definition '{definition}'.")
        name = parts[0]
        data_type = parts[1].upper()
        primary_key = "PRIMARY" in [part.upper() for part in parts]
        return Column(name=name, data_type=data_type, primary_key=primary_key)

    def _parse_value(self, raw_value: str) -> Value:
        token = raw_value.strip()
        if token.upper() == "NULL":
            return None
        if re.fullmatch(r"-?\d+", token):
            return int(token)
        if (token.startswith("'") and token.endswith("'")) or (
            token.startswith('"') and token.endswith('"')
        ):
            return token[1:-1]
        raise SQLParseError(f"Unsupported literal '{raw_value}'.")

    def _split_csv(self, payload: str) -> list[str]:
        items: list[str] = []
        current: list[str] = []
        quote_char: str | None = None
        for char in payload:
            if char in {"'", '"'}:
                if quote_char == char:
                    quote_char = None
                elif quote_char is None:
                    quote_char = char
            if char == "," and quote_char is None:
                items.append("".join(current).strip())
                current = []
                continue
            current.append(char)
        if current:
            items.append("".join(current).strip())
        return items

