"""
A small SQL front-end: tokenizer + recursive-descent parser producing an AST.

Supported grammar (a practical subset):
  CREATE TABLE t (col TYPE [PRIMARY KEY], ...)
  CREATE INDEX ON t (col)
  INSERT INTO t [(cols)] VALUES (...), (...)
  SELECT  <* | expr_list>  FROM t [ [INNER] JOIN t2 ON a = b ]
          [WHERE predicate] [GROUP BY cols]
  DELETE FROM t [WHERE predicate]
  BEGIN | COMMIT | ABORT|ROLLBACK

predicate := comparison (AND comparison)*
comparison:= operand OP operand          OP in = <> != < <= > >=
operand   := column | literal
Aggregates in the SELECT list: COUNT(*), COUNT(col), SUM/AVG/MIN/MAX(col).
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any, List, Optional, Tuple

KEYWORDS = {
    "CREATE", "TABLE", "INDEX", "ON", "INSERT", "INTO", "VALUES", "SELECT",
    "FROM", "WHERE", "JOIN", "INNER", "DELETE", "AND", "GROUP", "BY",
    "PRIMARY", "KEY", "BEGIN", "COMMIT", "ABORT", "ROLLBACK", "AS",
    "INT", "FLOAT", "TEXT", "BOOL", "TRUE", "FALSE", "NULL",
}
AGGREGATES = {"COUNT", "SUM", "AVG", "MIN", "MAX"}

_TOKEN_RE = re.compile(
    r"""\s*(?:
        (?P<float>-?\d+\.\d+)
      | (?P<int>-?\d+)
      | (?P<string>'(?:[^']|'')*')
      | (?P<op><=|>=|<>|!=|=|<|>)
      | (?P<punct>[(),.*])
      | (?P<ident>[A-Za-z_][A-Za-z_0-9]*)
    )""",
    re.VERBOSE,
)


@dataclass
class Token:
    kind: str
    value: Any


def tokenize(sql: str) -> List[Token]:
    pos = 0
    toks: List[Token] = []
    sql = sql.strip().rstrip(";")
    while pos < len(sql):
        if sql[pos].isspace():
            pos += 1
            continue
        m = _TOKEN_RE.match(sql, pos)
        if not m or m.end() == pos:
            raise SyntaxError(f"cannot tokenize near: {sql[pos:pos+20]!r}")
        pos = m.end()
        if m.lastgroup == "float":
            toks.append(Token("number", float(m.group())))
        elif m.lastgroup == "int":
            toks.append(Token("number", int(m.group())))
        elif m.lastgroup == "string":
            toks.append(Token("string", m.group()[1:-1].replace("''", "'")))
        elif m.lastgroup == "op":
            toks.append(Token("op", m.group()))
        elif m.lastgroup == "punct":
            toks.append(Token("punct", m.group()))
        else:
            word = m.group()
            up = word.upper()
            if up in KEYWORDS or up in AGGREGATES:
                toks.append(Token("kw", up))
            else:
                toks.append(Token("ident", word))
    toks.append(Token("eof", None))
    return toks


# ---- AST -----------------------------------------------------------------
@dataclass
class ColumnRef:
    table: Optional[str]
    name: str


@dataclass
class Literal:
    value: Any


@dataclass
class Comparison:
    left: Any
    op: str
    right: Any


@dataclass
class Predicate:
    conjuncts: List[Comparison]  # ANDed together


@dataclass
class Aggregate:
    func: str
    arg: Optional[ColumnRef]  # None means COUNT(*)


@dataclass
class CreateTable:
    name: str
    columns: List[Tuple[str, str]]
    primary_key: Optional[str]


@dataclass
class CreateIndex:
    table: str
    column: str


@dataclass
class Insert:
    table: str
    columns: Optional[List[str]]
    rows: List[List[Any]]


@dataclass
class Join:
    table: str
    left: ColumnRef
    right: ColumnRef


@dataclass
class Select:
    projections: List[Any]  # ColumnRef | Aggregate | "*"
    table: str
    joins: List[Join]
    where: Optional[Predicate]
    group_by: List[ColumnRef] = field(default_factory=list)
    aliases: dict = field(default_factory=dict)  # alias/name -> real table


@dataclass
class Delete:
    table: str
    where: Optional[Predicate]


@dataclass
class TxnControl:
    action: str  # BEGIN / COMMIT / ABORT


class Parser:
    def __init__(self, sql: str):
        self.toks = tokenize(sql)
        self.i = 0

    def peek(self) -> Token:
        return self.toks[self.i]

    def next(self) -> Token:
        t = self.toks[self.i]
        self.i += 1
        return t

    def expect(self, kind: str, value: Any = None) -> Token:
        t = self.next()
        if t.kind != kind or (value is not None and t.value != value):
            raise SyntaxError(f"expected {kind} {value}, got {t.kind} {t.value}")
        return t

    def accept(self, kind: str, value: Any = None) -> bool:
        t = self.peek()
        if t.kind == kind and (value is None or t.value == value):
            self.i += 1
            return True
        return False

    def parse(self):
        t = self.peek()
        if t.kind == "kw" and t.value == "CREATE":
            return self._create()
        if t.kind == "kw" and t.value == "INSERT":
            return self._insert()
        if t.kind == "kw" and t.value == "SELECT":
            return self._select()
        if t.kind == "kw" and t.value == "DELETE":
            return self._delete()
        if t.kind == "kw" and t.value in ("BEGIN", "COMMIT", "ABORT", "ROLLBACK"):
            self.next()
            action = "ABORT" if t.value == "ROLLBACK" else t.value
            return TxnControl(action)
        raise SyntaxError(f"unexpected start of statement: {t.value}")

    # ---- statements ------------------------------------------------------
    def _create(self):
        self.expect("kw", "CREATE")
        if self.accept("kw", "INDEX"):
            self.expect("kw", "ON")
            table = self.expect("ident").value
            self.expect("punct", "(")
            col = self.expect("ident").value
            self.expect("punct", ")")
            return CreateIndex(table, col)
        self.expect("kw", "TABLE")
        name = self.expect("ident").value
        self.expect("punct", "(")
        cols: List[Tuple[str, str]] = []
        pk: Optional[str] = None
        while True:
            cname = self.expect("ident").value
            ctype = self.next()
            if ctype.kind != "kw" or ctype.value not in ("INT", "FLOAT", "TEXT", "BOOL"):
                raise SyntaxError(f"bad column type: {ctype.value}")
            cols.append((cname, ctype.value))
            if self.accept("kw", "PRIMARY"):
                self.expect("kw", "KEY")
                pk = cname
            if self.accept("punct", ","):
                continue
            break
        self.expect("punct", ")")
        return CreateTable(name, cols, pk)

    def _insert(self):
        self.expect("kw", "INSERT")
        self.expect("kw", "INTO")
        table = self.expect("ident").value
        columns = None
        if self.accept("punct", "("):
            columns = [self.expect("ident").value]
            while self.accept("punct", ","):
                columns.append(self.expect("ident").value)
            self.expect("punct", ")")
        self.expect("kw", "VALUES")
        rows = [self._value_tuple()]
        while self.accept("punct", ","):
            rows.append(self._value_tuple())
        return Insert(table, columns, rows)

    def _value_tuple(self) -> List[Any]:
        self.expect("punct", "(")
        vals = [self._literal()]
        while self.accept("punct", ","):
            vals.append(self._literal())
        self.expect("punct", ")")
        return vals

    def _literal(self):
        t = self.next()
        if t.kind == "number":
            return t.value
        if t.kind == "string":
            return t.value
        if t.kind == "kw" and t.value in ("TRUE", "FALSE"):
            return t.value == "TRUE"
        if t.kind == "kw" and t.value == "NULL":
            return None
        raise SyntaxError(f"expected literal, got {t.kind} {t.value}")

    def _table_with_alias(self):
        name = self.expect("ident").value
        alias = None
        if self.accept("kw", "AS"):
            alias = self.expect("ident").value
        elif self.peek().kind == "ident":
            alias = self.next().value
        return name, alias

    def _select(self):
        self.expect("kw", "SELECT")
        projs: List[Any] = []
        if self.accept("punct", "*"):
            projs.append("*")
        else:
            projs.append(self._projection())
            while self.accept("punct", ","):
                projs.append(self._projection())
        self.expect("kw", "FROM")
        table, alias = self._table_with_alias()
        aliases = {table: table}
        if alias:
            aliases[alias] = table
        join_list: List[Join] = []
        while self.peek().kind == "kw" and self.peek().value in ("JOIN", "INNER"):
            self.accept("kw", "INNER")
            self.expect("kw", "JOIN")
            jtable, jalias = self._table_with_alias()
            aliases[jtable] = jtable
            if jalias:
                aliases[jalias] = jtable
            self.expect("kw", "ON")
            left = self._column_ref()
            self.expect("op", "=")
            right = self._column_ref()
            join_list.append(Join(jtable, left, right))
        where = None
        if self.accept("kw", "WHERE"):
            where = self._predicate()
        group_by: List[ColumnRef] = []
        if self.accept("kw", "GROUP"):
            self.expect("kw", "BY")
            group_by.append(self._column_ref())
            while self.accept("punct", ","):
                group_by.append(self._column_ref())
        return Select(projs, table, join_list, where, group_by, aliases)

    def _projection(self):
        t = self.peek()
        if t.kind == "kw" and t.value in AGGREGATES:
            self.next()
            func = t.value
            self.expect("punct", "(")
            if self.accept("punct", "*"):
                arg = None
            else:
                arg = self._column_ref()
            self.expect("punct", ")")
            return Aggregate(func, arg)
        return self._column_ref()

    def _delete(self):
        self.expect("kw", "DELETE")
        self.expect("kw", "FROM")
        table = self.expect("ident").value
        where = None
        if self.accept("kw", "WHERE"):
            where = self._predicate()
        return Delete(table, where)

    # ---- expressions -----------------------------------------------------
    def _predicate(self) -> Predicate:
        conj = [self._comparison()]
        while self.accept("kw", "AND"):
            conj.append(self._comparison())
        return Predicate(conj)

    def _comparison(self) -> Comparison:
        left = self._operand()
        op = self.expect("op").value
        if op == "!=":
            op = "<>"
        right = self._operand()
        return Comparison(left, op, right)

    def _operand(self):
        t = self.peek()
        if t.kind in ("number", "string"):
            self.next()
            return Literal(t.value)
        if t.kind == "kw" and t.value in ("TRUE", "FALSE", "NULL"):
            self.next()
            return Literal(None if t.value == "NULL" else t.value == "TRUE")
        return self._column_ref()

    def _column_ref(self) -> ColumnRef:
        first = self.expect("ident").value
        if self.accept("punct", "."):
            second = self.expect("ident").value
            return ColumnRef(first, second)
        return ColumnRef(None, first)


def parse(sql: str):
    p = Parser(sql)
    stmt = p.parse()
    if p.peek().kind != "eof":
        raise SyntaxError(f"unexpected trailing input near {p.peek().value!r}")
    return stmt
