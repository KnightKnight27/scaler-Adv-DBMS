"""
Hand-written SQL parser (recursive descent + tokenizer).
Supports:
  SELECT col,... FROM table [JOIN table ON a.x = b.y] [WHERE expr]
  INSERT INTO table (col,...) VALUES (val,...)
  DELETE FROM table [WHERE expr]
  CREATE TABLE table (col type [PRIMARY KEY], ...)
  DROP TABLE table
  BEGIN / COMMIT / ROLLBACK

WHERE expressions: col op val [AND col op val ...]
  op: = != < > <= >=
"""
import re

# ── tokens ────────────────────────────────────────────────────────────────────

TOKEN_RE = re.compile(
    r"'[^']*'"          # string literal
    r'|\d+\.\d+'        # float
    r'|\d+'             # integer
    r'|[<>!=]=?'        # operators
    r'|[(),.*]'         # punctuation
    r'|[A-Za-z_]\w*'   # identifiers / keywords
)

KEYWORDS = {
    'SELECT', 'FROM', 'WHERE', 'INSERT', 'INTO', 'VALUES', 'DELETE',
    'CREATE', 'TABLE', 'DROP', 'JOIN', 'ON', 'AND', 'OR', 'NOT',
    'PRIMARY', 'KEY', 'BEGIN', 'COMMIT', 'ROLLBACK', 'INT', 'TEXT',
    'FLOAT', 'AS', 'ORDER', 'BY', 'ASC', 'DESC', 'INNER', 'LEFT',
}


def tokenize(sql: str) -> list[str]:
    return TOKEN_RE.findall(sql)


# ── AST nodes ─────────────────────────────────────────────────────────────────

class SelectStmt:
    def __init__(self, columns, table, joins, where, order_by=None):
        self.columns = columns   # ['*'] or ['col1', 'col2'] or ['t.col']
        self.table = table       # 'tablename'
        self.joins = joins       # list of JoinClause
        self.where = where       # list of Condition (AND-ed together)
        self.order_by = order_by # (col, 'ASC'|'DESC') or None


class JoinClause:
    def __init__(self, table, left_col, right_col):
        self.table = table         # joined table name
        self.left_col = left_col   # 'left_table.col' or 'col'
        self.right_col = right_col


class Condition:
    def __init__(self, left, op, right):
        self.left = left   # column name (possibly 'table.col')
        self.op = op       # '=' '!=' '<' '>' '<=' '>='
        self.right = right # literal value (str/int/float) or 'col_ref'

    def __repr__(self):
        return f"Condition({self.left} {self.op} {self.right!r})"


class InsertStmt:
    def __init__(self, table, columns, values):
        self.table = table
        self.columns = columns  # list of col names
        self.values = values    # list of values


class DeleteStmt:
    def __init__(self, table, where):
        self.table = table
        self.where = where  # list of Condition


class CreateTableStmt:
    def __init__(self, table, col_defs):
        self.table = table
        self.col_defs = col_defs  # list of {'name', 'type', 'primary_key'}


class DropTableStmt:
    def __init__(self, table):
        self.table = table


class BeginStmt:
    pass


class CommitStmt:
    pass


class RollbackStmt:
    pass


# ── parser ────────────────────────────────────────────────────────────────────

class Parser:
    def __init__(self, tokens: list[str]):
        self.tokens = tokens
        self.pos = 0

    def peek(self) -> str | None:
        return self.tokens[self.pos] if self.pos < len(self.tokens) else None

    def consume(self, expected: str = None) -> str:
        tok = self.peek()
        if tok is None:
            raise SyntaxError(f"Unexpected end of input, expected {expected!r}")
        if expected and tok.upper() != expected.upper():
            raise SyntaxError(f"Expected {expected!r}, got {tok!r}")
        self.pos += 1
        return tok

    def consume_any(self) -> str:
        tok = self.peek()
        if tok is None:
            raise SyntaxError("Unexpected end of input")
        self.pos += 1
        return tok

    def match(self, *keywords) -> bool:
        tok = self.peek()
        return tok is not None and tok.upper() in {k.upper() for k in keywords}

    # ── entry point ───────────────────────────────────────────────────────────

    def parse(self):
        kw = self.peek()
        if kw is None:
            raise SyntaxError("Empty query")
        kw = kw.upper()
        if kw == 'SELECT':
            return self.parse_select()
        if kw == 'INSERT':
            return self.parse_insert()
        if kw == 'DELETE':
            return self.parse_delete()
        if kw == 'CREATE':
            return self.parse_create_table()
        if kw == 'DROP':
            return self.parse_drop_table()
        if kw == 'BEGIN':
            self.consume('BEGIN')
            return BeginStmt()
        if kw == 'COMMIT':
            self.consume('COMMIT')
            return CommitStmt()
        if kw == 'ROLLBACK':
            self.consume('ROLLBACK')
            return RollbackStmt()
        raise SyntaxError(f"Unknown statement: {kw}")

    # ── SELECT ────────────────────────────────────────────────────────────────

    def parse_select(self):
        self.consume('SELECT')
        columns = self.parse_column_list()
        self.consume('FROM')
        table = self.consume_any()
        joins = []
        while self.match('JOIN', 'INNER', 'LEFT'):
            if self.match('INNER', 'LEFT'):
                self.consume_any()
            self.consume('JOIN')
            joins.append(self.parse_join())
        where = []
        if self.match('WHERE'):
            self.consume('WHERE')
            where = self.parse_conditions()
        order_by = None
        if self.match('ORDER'):
            self.consume('ORDER')
            self.consume('BY')
            col = self.consume_any()
            direction = 'ASC'
            if self.match('ASC', 'DESC'):
                direction = self.consume_any().upper()
            order_by = (col, direction)
        return SelectStmt(columns, table, joins, where, order_by)

    def parse_column_list(self) -> list[str]:
        if self.match('*'):
            self.consume('*')
            return ['*']
        cols = [self.consume_any()]
        while self.match(','):
            self.consume(',')
            cols.append(self.consume_any())
        return cols

    def parse_join(self) -> JoinClause:
        table = self.consume_any()
        self.consume('ON')
        left = self.consume_any()
        if self.match('.'):
            self.consume('.')
            left = left + '.' + self.consume_any()
        self.consume('=')
        right = self.consume_any()
        if self.match('.'):
            self.consume('.')
            right = right + '.' + self.consume_any()
        return JoinClause(table, left, right)

    # ── WHERE conditions ──────────────────────────────────────────────────────

    def parse_conditions(self) -> list[Condition]:
        conds = [self.parse_condition()]
        while self.match('AND'):
            self.consume('AND')
            conds.append(self.parse_condition())
        return conds

    def parse_condition(self) -> Condition:
        left = self.consume_any()
        if self.match('.'):
            self.consume('.')
            left = left + '.' + self.consume_any()
        op = self.consume_any()
        if op not in ('=', '!=', '<', '>', '<=', '>='):
            raise SyntaxError(f"Invalid operator: {op}")
        right = self.parse_value()
        return Condition(left, op, right)

    def parse_value(self):
        tok = self.consume_any()
        if tok.startswith("'") and tok.endswith("'"):
            return tok[1:-1]  # strip quotes
        try:
            if '.' in tok:
                return float(tok)
            return int(tok)
        except ValueError:
            return tok  # column reference

    # ── INSERT ────────────────────────────────────────────────────────────────

    def parse_insert(self):
        self.consume('INSERT')
        self.consume('INTO')
        table = self.consume_any()
        self.consume('(')
        cols = [self.consume_any()]
        while self.match(','):
            self.consume(',')
            cols.append(self.consume_any())
        self.consume(')')
        self.consume('VALUES')
        self.consume('(')
        vals = [self.parse_value()]
        while self.match(','):
            self.consume(',')
            vals.append(self.parse_value())
        self.consume(')')
        return InsertStmt(table, cols, vals)

    # ── DELETE ────────────────────────────────────────────────────────────────

    def parse_delete(self):
        self.consume('DELETE')
        self.consume('FROM')
        table = self.consume_any()
        where = []
        if self.match('WHERE'):
            self.consume('WHERE')
            where = self.parse_conditions()
        return DeleteStmt(table, where)

    # ── CREATE TABLE ──────────────────────────────────────────────────────────

    def parse_create_table(self):
        self.consume('CREATE')
        self.consume('TABLE')
        table = self.consume_any()
        self.consume('(')
        col_defs = [self.parse_col_def()]
        while self.match(','):
            self.consume(',')
            col_defs.append(self.parse_col_def())
        self.consume(')')
        return CreateTableStmt(table, col_defs)

    def parse_col_def(self) -> dict:
        name = self.consume_any()
        col_type = self.consume_any().upper()
        primary_key = False
        if self.match('PRIMARY'):
            self.consume('PRIMARY')
            self.consume('KEY')
            primary_key = True
        return {'name': name, 'type': col_type, 'primary_key': primary_key}

    # ── DROP TABLE ────────────────────────────────────────────────────────────

    def parse_drop_table(self):
        self.consume('DROP')
        self.consume('TABLE')
        table = self.consume_any()
        return DropTableStmt(table)


def parse(sql: str):
    tokens = tokenize(sql.strip().rstrip(';'))
    return Parser(tokens).parse()
