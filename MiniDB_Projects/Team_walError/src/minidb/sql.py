"""sql.py — tokenizer, AST, and recursive-descent parser for MiniDB's SQL subset.

Supported grammar (case-insensitive keywords):

    CREATE TABLE t (col TYPE [PRIMARY KEY] [NOT NULL], ...)
    CREATE INDEX ON t (col)
    INSERT INTO t [(c1, c2, ...)] VALUES (v, ...), (v, ...), ...
    SELECT  * | item [, item]...  FROM t [alias]
            [JOIN t2 [alias] ON expr] ...  [WHERE expr]
    DELETE FROM t [WHERE expr]
    BEGIN [TRANSACTION] | COMMIT | ROLLBACK

    item ::= [table.]column [AS alias] | [table.]*
    expr ::= or_expr ; OR < AND < comparison (=, <>, !=, <, <=, >, >=)
    operand ::= literal | [table.]column | ( expr )
    literal ::= integer | float | 'string' | TRUE | FALSE | NULL

The parser produces dataclass AST nodes consumed by plan.py / executor.py.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from .engine import MiniDBError
from .types import ColumnType


class ParseError(MiniDBError):
    """Raised on a lexing or parsing error."""


# =============================================================================
# Tokenizer
# =============================================================================

_KEYWORDS = {
    "CREATE", "TABLE", "INDEX", "ON", "INSERT", "INTO", "VALUES", "SELECT",
    "FROM", "WHERE", "JOIN", "INNER", "AS", "DELETE", "PRIMARY", "KEY", "NOT",
    "NULL", "AND", "OR", "TRUE", "FALSE", "BEGIN", "TRANSACTION", "COMMIT",
    "ROLLBACK", "EXPLAIN",
}

# multi-char operators must be tried before single-char ones
_OPERATORS = ["<=", ">=", "<>", "!=", "=", "<", ">"]
_PUNCT = set("(),.*;")


@dataclass(frozen=True)
class Token:
    kind: str   # 'kw' | 'ident' | 'int' | 'float' | 'str' | 'op' | 'punct' | 'eof'
    value: Any
    pos: int


def tokenize(sql: str) -> list[Token]:
    tokens: list[Token] = []
    i, n = 0, len(sql)
    while i < n:
        c = sql[i]
        if c.isspace():
            i += 1
            continue
        if c == "-" and i + 1 < n and sql[i + 1] == "-":  # -- line comment
            while i < n and sql[i] != "\n":
                i += 1
            continue
        # string literal
        if c == "'":
            j = i + 1
            buf = []
            while j < n:
                if sql[j] == "'":
                    if j + 1 < n and sql[j + 1] == "'":  # '' -> escaped quote
                        buf.append("'")
                        j += 2
                        continue
                    break
                buf.append(sql[j])
                j += 1
            if j >= n:
                raise ParseError(f"unterminated string literal at {i}")
            tokens.append(Token("str", "".join(buf), i))
            i = j + 1
            continue
        # number
        if c.isdigit() or (c == "-" and i + 1 < n and sql[i + 1].isdigit()
                           and _prev_allows_sign(tokens)):
            j = i + 1
            is_float = False
            while j < n and (sql[j].isdigit() or sql[j] in ".eE+-"):
                if sql[j] in ".eE":
                    is_float = True
                # stop a trailing +/- that isn't part of an exponent
                if sql[j] in "+-" and sql[j - 1] not in "eE":
                    break
                j += 1
            text = sql[i:j]
            try:
                tokens.append(Token("float", float(text), i) if is_float
                              else Token("int", int(text), i))
            except ValueError:
                raise ParseError(f"bad number {text!r} at {i}")
            i = j
            continue
        # identifier / keyword
        if c.isalpha() or c == "_":
            j = i + 1
            while j < n and (sql[j].isalnum() or sql[j] == "_"):
                j += 1
            word = sql[i:j]
            up = word.upper()
            if up in _KEYWORDS:
                tokens.append(Token("kw", up, i))
            else:
                tokens.append(Token("ident", word, i))
            i = j
            continue
        # operators
        for op in _OPERATORS:
            if sql.startswith(op, i):
                tokens.append(Token("op", op, i))
                i += len(op)
                break
        else:
            if c in _PUNCT:
                tokens.append(Token("punct", c, i))
                i += 1
            else:
                raise ParseError(f"unexpected character {c!r} at {i}")
    tokens.append(Token("eof", None, n))
    return tokens


def _prev_allows_sign(tokens: list[Token]) -> bool:
    """A leading '-' is a negative number only after an operator/punct/keyword."""
    if not tokens:
        return True
    last = tokens[-1]
    if last.kind in ("op",):
        return True
    if last.kind == "punct" and last.value in "(,":
        return True
    if last.kind == "kw" and last.value in ("VALUES", "AND", "OR", "WHERE", "ON"):
        return True
    return False


# =============================================================================
# AST
# =============================================================================


@dataclass
class ColumnRef:
    name: str
    table: str | None = None


@dataclass
class Literal:
    value: Any


@dataclass
class Comparison:
    left: Any
    op: str
    right: Any


@dataclass
class And:
    left: Any
    right: Any


@dataclass
class Or:
    left: Any
    right: Any


@dataclass
class ColumnDef:
    name: str
    type: ColumnType
    primary_key: bool = False
    nullable: bool = True


@dataclass
class CreateTable:
    table: str
    columns: list[ColumnDef]


@dataclass
class CreateIndex:
    table: str
    column: str


@dataclass
class Insert:
    table: str
    columns: list[str] | None       # None => all columns in schema order
    rows: list[list[Any]]           # each row is a list of literal Python values


@dataclass
class SelectItem:
    expr: ColumnRef                 # a column reference
    alias: str | None = None


@dataclass
class JoinClause:
    table: str
    alias: str | None
    on: Any                         # an expression (usually a Comparison)


@dataclass
class Select:
    star: bool
    items: list[SelectItem]
    from_table: str
    from_alias: str | None = None
    joins: list[JoinClause] = field(default_factory=list)
    where: Any | None = None


@dataclass
class Delete:
    table: str
    where: Any | None = None


@dataclass
class Explain:
    query: "Select"


@dataclass
class Begin:
    pass


@dataclass
class Commit:
    pass


@dataclass
class Rollback:
    pass


_TYPE_MAP = {
    "INT": ColumnType.INT, "INTEGER": ColumnType.INT,
    "FLOAT": ColumnType.FLOAT, "REAL": ColumnType.FLOAT, "DOUBLE": ColumnType.FLOAT,
    "TEXT": ColumnType.TEXT, "VARCHAR": ColumnType.TEXT, "STRING": ColumnType.TEXT,
    "BOOL": ColumnType.BOOL, "BOOLEAN": ColumnType.BOOL,
}


# =============================================================================
# Parser
# =============================================================================


class Parser:
    def __init__(self, tokens: list[Token]) -> None:
        self.toks = tokens
        self.i = 0

    # --- token helpers -----------------------------------------------------

    @property
    def cur(self) -> Token:
        return self.toks[self.i]

    def advance(self) -> Token:
        t = self.toks[self.i]
        self.i += 1
        return t

    def expect(self, kind: str, value: Any = None) -> Token:
        t = self.cur
        if t.kind != kind or (value is not None and t.value != value):
            want = value if value is not None else kind
            raise ParseError(f"expected {want!r}, got {t.value!r} (at {t.pos})")
        return self.advance()

    def accept(self, kind: str, value: Any = None) -> Token | None:
        t = self.cur
        if t.kind == kind and (value is None or t.value == value):
            return self.advance()
        return None

    def at_kw(self, *words: str) -> bool:
        return self.cur.kind == "kw" and self.cur.value in words

    # --- entry -------------------------------------------------------------

    def parse_statement(self):
        if self.accept("kw", "EXPLAIN"):
            return Explain(self._select())
        if self.at_kw("CREATE"):
            return self._create()
        if self.at_kw("INSERT"):
            return self._insert()
        if self.at_kw("SELECT"):
            return self._select()
        if self.at_kw("DELETE"):
            return self._delete()
        if self.accept("kw", "BEGIN"):
            self.accept("kw", "TRANSACTION")
            return Begin()
        if self.accept("kw", "COMMIT"):
            return Commit()
        if self.accept("kw", "ROLLBACK"):
            return Rollback()
        raise ParseError(f"unexpected statement start: {self.cur.value!r}")

    def _ident(self) -> str:
        return self.expect("ident").value

    # --- CREATE ------------------------------------------------------------

    def _create(self):
        self.expect("kw", "CREATE")
        if self.accept("kw", "TABLE"):
            return self._create_table()
        if self.accept("kw", "INDEX"):
            self.expect("kw", "ON")
            table = self._ident()
            self.expect("punct", "(")
            column = self._ident()
            self.expect("punct", ")")
            return CreateIndex(table, column)
        raise ParseError("expected TABLE or INDEX after CREATE")

    def _create_table(self) -> CreateTable:
        table = self._ident()
        self.expect("punct", "(")
        columns: list[ColumnDef] = []
        while True:
            name = self._ident()
            type_tok = self.expect("ident")
            type_name = type_tok.value.upper()
            if type_name not in _TYPE_MAP:
                raise ParseError(f"unknown type {type_tok.value!r}")
            col = ColumnDef(name=name, type=_TYPE_MAP[type_name])
            # constraints
            while True:
                if self.accept("kw", "PRIMARY"):
                    self.expect("kw", "KEY")
                    col = ColumnDef(col.name, col.type, primary_key=True, nullable=False)
                elif self.accept("kw", "NOT"):
                    self.expect("kw", "NULL")
                    col = ColumnDef(col.name, col.type, primary_key=col.primary_key,
                                    nullable=False)
                else:
                    break
            columns.append(col)
            if not self.accept("punct", ","):
                break
        self.expect("punct", ")")
        if not columns:
            raise ParseError("CREATE TABLE needs at least one column")
        return CreateTable(table, columns)

    # --- INSERT ------------------------------------------------------------

    def _insert(self) -> Insert:
        self.expect("kw", "INSERT")
        self.expect("kw", "INTO")
        table = self._ident()
        columns: list[str] | None = None
        if self.accept("punct", "("):
            columns = [self._ident()]
            while self.accept("punct", ","):
                columns.append(self._ident())
            self.expect("punct", ")")
        self.expect("kw", "VALUES")
        rows = [self._value_tuple()]
        while self.accept("punct", ","):
            rows.append(self._value_tuple())
        return Insert(table, columns, rows)

    def _value_tuple(self) -> list[Any]:
        self.expect("punct", "(")
        vals = [self._literal_value()]
        while self.accept("punct", ","):
            vals.append(self._literal_value())
        self.expect("punct", ")")
        return vals

    def _literal_value(self) -> Any:
        lit = self._primary()
        if not isinstance(lit, Literal):
            raise ParseError("expected a literal value")
        return lit.value

    # --- SELECT ------------------------------------------------------------

    def _select(self) -> Select:
        self.expect("kw", "SELECT")
        star = False
        items: list[SelectItem] = []
        if self.accept("punct", "*"):
            star = True
        else:
            items.append(self._select_item())
            while self.accept("punct", ","):
                items.append(self._select_item())
        self.expect("kw", "FROM")
        from_table = self._ident()
        from_alias = self._maybe_alias()
        joins: list[JoinClause] = []
        while self.at_kw("JOIN", "INNER"):
            self.accept("kw", "INNER")
            self.expect("kw", "JOIN")
            jtable = self._ident()
            jalias = self._maybe_alias()
            self.expect("kw", "ON")
            on = self._expr()
            joins.append(JoinClause(jtable, jalias, on))
        where = None
        if self.accept("kw", "WHERE"):
            where = self._expr()
        return Select(star, items, from_table, from_alias, joins, where)

    def _select_item(self) -> SelectItem:
        ref = self._colref()
        alias = None
        if self.accept("kw", "AS"):
            alias = self._ident()
        return SelectItem(ref, alias)

    def _maybe_alias(self) -> str | None:
        if self.accept("kw", "AS"):
            return self._ident()
        # a bare identifier right after a table name is an alias
        if self.cur.kind == "ident":
            return self.advance().value
        return None

    # --- DELETE ------------------------------------------------------------

    def _delete(self) -> Delete:
        self.expect("kw", "DELETE")
        self.expect("kw", "FROM")
        table = self._ident()
        where = None
        if self.accept("kw", "WHERE"):
            where = self._expr()
        return Delete(table, where)

    # --- expressions -------------------------------------------------------

    def _expr(self):
        return self._or()

    def _or(self):
        left = self._and()
        while self.accept("kw", "OR"):
            left = Or(left, self._and())
        return left

    def _and(self):
        left = self._cmp()
        while self.accept("kw", "AND"):
            left = And(left, self._cmp())
        return left

    def _cmp(self):
        left = self._primary()
        if self.cur.kind == "op":
            op = self.advance().value
            right = self._primary()
            return Comparison(left, op, right)
        return left

    def _primary(self):
        if self.accept("punct", "("):
            e = self._expr()
            self.expect("punct", ")")
            return e
        t = self.cur
        if t.kind == "int" or t.kind == "float" or t.kind == "str":
            self.advance()
            return Literal(t.value)
        if t.kind == "kw" and t.value in ("TRUE", "FALSE"):
            self.advance()
            return Literal(t.value == "TRUE")
        if t.kind == "kw" and t.value == "NULL":
            self.advance()
            return Literal(None)
        if t.kind == "ident":
            return self._colref()
        raise ParseError(f"unexpected token {t.value!r} (at {t.pos})")

    def _colref(self) -> ColumnRef:
        name = self._ident()
        if self.accept("punct", "."):
            if self.accept("punct", "*"):
                return ColumnRef("*", table=name)  # table.* (rare; handled by exec)
            col = self._ident()
            return ColumnRef(col, table=name)
        return ColumnRef(name)


def parse(sql: str):
    """Parse a single SQL statement string into an AST node."""
    tokens = tokenize(sql)
    parser = Parser(tokens)
    if parser.cur.kind == "eof":
        raise ParseError("empty statement")
    stmt = parser.parse_statement()
    parser.accept("punct", ";")
    if parser.cur.kind != "eof":
        raise ParseError(f"unexpected trailing input: {parser.cur.value!r}")
    return stmt
