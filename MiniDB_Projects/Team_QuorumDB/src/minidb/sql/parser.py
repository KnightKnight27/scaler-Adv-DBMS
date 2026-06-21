"""A hand-written tokenizer + recursive-descent parser for MiniDB SQL.

Small enough to read top-to-bottom, it produces the AST nodes in ``ast.py``.
SQL keywords are case-insensitive; identifiers are returned as written.
"""

from __future__ import annotations

from typing import Any, List, NamedTuple, Optional

from . import ast

_KEYWORDS = {
    "CREATE", "TABLE", "INDEX", "ON", "UNIQUE", "DROP", "PRIMARY", "KEY",
    "NOT", "NULL", "INT", "INTEGER", "FLOAT", "DOUBLE", "TEXT", "VARCHAR",
    "BOOL", "BOOLEAN", "INSERT", "INTO", "VALUES", "DELETE", "FROM", "SELECT",
    "WHERE", "AND", "JOIN", "INNER", "BEGIN", "COMMIT", "ROLLBACK", "ABORT",
    "TRUE", "FALSE", "AS",
}
_TYPE_KEYWORDS = {"INT": "INT", "INTEGER": "INT", "FLOAT": "FLOAT",
                  "DOUBLE": "FLOAT", "TEXT": "TEXT", "VARCHAR": "TEXT",
                  "BOOL": "BOOL", "BOOLEAN": "BOOL"}


class ParseError(Exception):
    pass


class Token(NamedTuple):
    kind: str          # 'kw' | 'ident' | 'num' | 'str' | 'op' | 'punct' | 'eof'
    value: Any


def tokenize(sql: str) -> List[Token]:
    tokens: List[Token] = []
    i, n = 0, len(sql)
    while i < n:
        c = sql[i]
        if c.isspace():
            i += 1
            continue
        if c == "-" and i + 1 < n and sql[i + 1] == "-":   # line comment
            while i < n and sql[i] != "\n":
                i += 1
            continue
        if c in "().,;*":
            tokens.append(Token("punct", c))
            i += 1
            continue
        if c in "=<>!":
            two = sql[i:i + 2]
            if two in ("<=", ">=", "<>", "!="):
                tokens.append(Token("op", "!=" if two in ("<>", "!=") else two))
                i += 2
            elif c == "=":
                tokens.append(Token("op", "="))
                i += 1
            elif c in "<>":
                tokens.append(Token("op", c))
                i += 1
            else:
                raise ParseError(f"unexpected character {c!r}")
            continue
        if c in "'\"":                                     # string literal
            i += 1
            start = i
            buf = []
            while i < n and sql[i] != c:
                buf.append(sql[i])
                i += 1
            if i >= n:
                raise ParseError("unterminated string literal")
            i += 1
            tokens.append(Token("str", "".join(buf)))
            continue
        if c.isdigit() or (c == "-" and i + 1 < n and sql[i + 1].isdigit()):
            start = i
            i += 1
            is_float = False
            while i < n and (sql[i].isdigit() or sql[i] == "."):
                if sql[i] == ".":
                    is_float = True
                i += 1
            text = sql[start:i]
            tokens.append(Token("num", float(text) if is_float else int(text)))
            continue
        if c.isalpha() or c == "_":
            start = i
            i += 1
            while i < n and (sql[i].isalnum() or sql[i] == "_"):
                i += 1
            word = sql[start:i]
            up = word.upper()
            tokens.append(Token("kw", up) if up in _KEYWORDS else Token("ident", word))
            continue
        raise ParseError(f"unexpected character {c!r}")
    tokens.append(Token("eof", None))
    return tokens


class Parser:
    def __init__(self, sql: str):
        self.tokens = tokenize(sql)
        self.pos = 0

    # -- token helpers ------------------------------------------------------
    def _peek(self) -> Token:
        return self.tokens[self.pos]

    def _next(self) -> Token:
        tok = self.tokens[self.pos]
        self.pos += 1
        return tok

    def _accept_kw(self, *kw: str) -> bool:
        tok = self._peek()
        if tok.kind == "kw" and tok.value in kw:
            self.pos += 1
            return True
        return False

    def _expect_kw(self, *kw: str) -> str:
        tok = self._next()
        if tok.kind != "kw" or tok.value not in kw:
            raise ParseError(f"expected {' or '.join(kw)}, got {tok.value!r}")
        return tok.value

    def _expect_punct(self, ch: str) -> None:
        tok = self._next()
        if tok.kind != "punct" or tok.value != ch:
            raise ParseError(f"expected {ch!r}, got {tok.value!r}")

    def _accept_punct(self, ch: str) -> bool:
        tok = self._peek()
        if tok.kind == "punct" and tok.value == ch:
            self.pos += 1
            return True
        return False

    def _expect_ident(self) -> str:
        tok = self._next()
        if tok.kind != "ident":
            raise ParseError(f"expected identifier, got {tok.value!r}")
        return tok.value

    # -- entry points -------------------------------------------------------
    def parse(self):
        stmt = self._statement()
        self._accept_punct(";")
        if self._peek().kind != "eof":
            raise ParseError("trailing tokens after statement")
        return stmt

    @staticmethod
    def parse_script(sql: str) -> List[Any]:
        """Split a string of ;-separated statements and parse each."""
        out = []
        for chunk in _split_statements(sql):
            if chunk.strip():
                out.append(Parser(chunk).parse())
        return out

    def _statement(self):
        tok = self._peek()
        if tok.kind != "kw":
            raise ParseError(f"expected a statement, got {tok.value!r}")
        kw = tok.value
        if kw == "CREATE":
            return self._create()
        if kw == "DROP":
            self._next(); self._expect_kw("TABLE")
            return ast.DropTable(self._expect_ident())
        if kw == "INSERT":
            return self._insert()
        if kw == "DELETE":
            return self._delete()
        if kw == "SELECT":
            return self._select()
        if kw == "BEGIN":
            self._next(); return ast.Begin()
        if kw == "COMMIT":
            self._next(); return ast.Commit()
        if kw in ("ROLLBACK", "ABORT"):
            self._next(); return ast.Rollback()
        raise ParseError(f"unsupported statement: {kw}")

    # -- DDL ----------------------------------------------------------------
    def _create(self):
        self._expect_kw("CREATE")
        if self._accept_kw("TABLE"):
            return self._create_table()
        unique = self._accept_kw("UNIQUE")
        self._expect_kw("INDEX")
        name = None
        if self._peek().kind == "ident":
            name = self._expect_ident()
        self._expect_kw("ON")
        table = self._expect_ident()
        self._expect_punct("(")
        column = self._expect_ident()
        self._expect_punct(")")
        return ast.CreateIndex(table=table, column=column, unique=unique, name=name)

    def _create_table(self):
        name = self._expect_ident()
        self._expect_punct("(")
        columns: List[ast.ColumnDef] = []
        pk: Optional[str] = None
        while True:
            col = self._expect_ident()
            type_tok = self._next()
            if type_tok.kind != "kw" or type_tok.value not in _TYPE_KEYWORDS:
                raise ParseError(f"expected a column type, got {type_tok.value!r}")
            col_type = _TYPE_KEYWORDS[type_tok.value]
            # optional VARCHAR(n) length — accepted and ignored
            if self._accept_punct("("):
                self._next()
                self._expect_punct(")")
            not_null = False
            is_pk = False
            while True:
                if self._accept_kw("PRIMARY"):
                    self._expect_kw("KEY")
                    is_pk = True
                    not_null = True
                elif self._accept_kw("NOT"):
                    self._expect_kw("NULL")
                    not_null = True
                else:
                    break
            if is_pk:
                pk = col
            columns.append(ast.ColumnDef(col, col_type, not_null=not_null,
                                         primary_key=is_pk))
            if self._accept_punct(","):
                continue
            break
        self._expect_punct(")")
        return ast.CreateTable(name=name, columns=columns, pk_column=pk)

    # -- DML ----------------------------------------------------------------
    def _insert(self):
        self._expect_kw("INSERT")
        self._expect_kw("INTO")
        table = self._expect_ident()
        columns: Optional[List[str]] = None
        if self._accept_punct("("):
            columns = [self._expect_ident()]
            while self._accept_punct(","):
                columns.append(self._expect_ident())
            self._expect_punct(")")
        self._expect_kw("VALUES")
        rows: List[List[Any]] = []
        while True:
            self._expect_punct("(")
            values = [self._value()]
            while self._accept_punct(","):
                values.append(self._value())
            self._expect_punct(")")
            rows.append(values)
            if self._accept_punct(","):
                continue
            break
        return ast.Insert(table=table, columns=columns, rows=rows)

    def _delete(self):
        self._expect_kw("DELETE")
        self._expect_kw("FROM")
        table = self._expect_ident()
        where = self._where()
        return ast.Delete(table=table, where=where)

    def _select(self):
        self._expect_kw("SELECT")
        columns: List[str] = []
        if self._accept_punct("*"):
            columns = ["*"]
        else:
            columns.append(self._column_name())
            while self._accept_punct(","):
                columns.append(self._column_name())
        self._expect_kw("FROM")
        from_table = self._expect_ident()
        from_alias = self._opt_alias()
        joins: List[ast.JoinClause] = []
        while True:
            self._accept_kw("INNER")
            if not self._accept_kw("JOIN"):
                break
            jtable = self._expect_ident()
            jalias = self._opt_alias()
            self._expect_kw("ON")
            on = self._comparison()
            if not on.is_join_predicate:
                raise ParseError("JOIN ON must compare two columns")
            joins.append(ast.JoinClause(table=jtable, on=on, alias=jalias))
        where = self._where()
        return ast.Select(columns=columns, from_table=from_table,
                          from_alias=from_alias, joins=joins, where=where)

    def _opt_alias(self) -> Optional[str]:
        """An optional table alias: 'AS x' or a bare identifier."""
        if self._accept_kw("AS"):
            return self._expect_ident()
        if self._peek().kind == "ident":
            return self._next().value
        return None

    # -- shared fragments ---------------------------------------------------
    def _column_name(self) -> str:
        """A possibly-qualified column reference, returned as written."""
        first = self._expect_ident()
        if self._accept_punct("."):
            second = self._expect_ident()
            return f"{first}.{second}"
        return first

    def _column_ref(self) -> ast.ColumnRef:
        first = self._expect_ident()
        if self._accept_punct("."):
            second = self._expect_ident()
            return ast.ColumnRef(name=second, table=first)
        return ast.ColumnRef(name=first)

    def _where(self) -> ast.Predicate:
        pred = ast.Predicate()
        if self._accept_kw("WHERE"):
            pred.comparisons.append(self._comparison())
            while self._accept_kw("AND"):
                pred.comparisons.append(self._comparison())
        return pred

    def _comparison(self) -> ast.Comparison:
        left = self._column_ref()
        op_tok = self._next()
        if op_tok.kind != "op":
            raise ParseError(f"expected a comparison operator, got {op_tok.value!r}")
        # right side: another column or a literal
        nxt = self._peek()
        if nxt.kind == "ident":
            right: Any = self._column_ref()
        else:
            right = ast.Literal(self._value())
        return ast.Comparison(left=left, op=op_tok.value, right=right)

    def _value(self) -> Any:
        tok = self._next()
        if tok.kind in ("num", "str"):
            return tok.value
        if tok.kind == "kw":
            if tok.value == "TRUE":
                return True
            if tok.value == "FALSE":
                return False
            if tok.value == "NULL":
                return None
        raise ParseError(f"expected a literal value, got {tok.value!r}")


def _split_statements(sql: str) -> List[str]:
    """Split on semicolons that are not inside string literals."""
    out, buf, quote = [], [], None
    for ch in sql:
        if quote:
            buf.append(ch)
            if ch == quote:
                quote = None
        elif ch in "'\"":
            quote = ch
            buf.append(ch)
        elif ch == ";":
            out.append("".join(buf))
            buf = []
        else:
            buf.append(ch)
    if buf:
        out.append("".join(buf))
    return out


def parse(sql: str):
    return Parser(sql).parse()
