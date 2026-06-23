"""
SQL Parser — Recursive descent parser for MiniDB's SQL dialect.

Converts a stream of tokens from the lexer into an AST (Abstract Syntax Tree).

Supported statements:
  - SELECT ... FROM ... [JOIN ...] [WHERE ...] [ORDER BY ...] [LIMIT ...]
  - INSERT INTO ... (cols) VALUES (vals), ...
  - DELETE FROM ... [WHERE ...]
  - UPDATE ... SET ... [WHERE ...]
  - CREATE TABLE ... (column_defs)
  - DROP TABLE ...
  - CREATE INDEX ... ON ... (column)
  - BEGIN / COMMIT / ROLLBACK
"""

from .lexer import Lexer, Token, TokenType, LexerError
from .ast_nodes import *
from typing import List, Optional


class ParseError(Exception):
    """Error during SQL parsing."""
    pass


class Parser:
    """
    Recursive descent SQL parser.

    Usage:
        parser = Parser("SELECT name, age FROM employees WHERE age > 30")
        ast = parser.parse()
    """

    def __init__(self, sql: str):
        self.sql = sql
        self.lexer = Lexer(sql)
        self.tokens: List[Token] = []
        self.pos = 0

    def parse(self) -> Statement:
        """
        Parse the SQL string and return an AST node.

        Returns:
            A Statement AST node.

        Raises:
            ParseError: If the SQL is syntactically invalid.
        """
        self.tokens = self.lexer.tokenize()
        self.pos = 0

        stmt = self._parse_statement()

        # Optional semicolon
        if self._check(TokenType.SEMICOLON):
            self._advance()

        return stmt

    def parse_multiple(self) -> List[Statement]:
        """Parse multiple semicolon-separated statements."""
        self.tokens = self.lexer.tokenize()
        self.pos = 0
        statements = []

        while not self._check(TokenType.EOF):
            stmt = self._parse_statement()
            statements.append(stmt)
            if self._check(TokenType.SEMICOLON):
                self._advance()

        return statements

    # ─── Token helpers ────────────────────────────────────────────────

    def _current(self) -> Token:
        if self.pos < len(self.tokens):
            return self.tokens[self.pos]
        return Token(TokenType.EOF, '', -1)

    def _peek(self) -> Token:
        if self.pos + 1 < len(self.tokens):
            return self.tokens[self.pos + 1]
        return Token(TokenType.EOF, '', -1)

    def _check(self, *types) -> bool:
        return self._current().type in types

    def _advance(self) -> Token:
        token = self._current()
        self.pos += 1
        return token

    def _expect(self, *types) -> Token:
        if not self._check(*types):
            expected = ' or '.join(t.name for t in types)
            raise ParseError(
                f"Expected {expected}, got {self._current().type.name} "
                f"('{self._current().value}') at position {self._current().position}"
            )
        return self._advance()

    def _match(self, *types) -> Optional[Token]:
        if self._check(*types):
            return self._advance()
        return None

    # ─── Statement parsing ────────────────────────────────────────────

    def _parse_statement(self) -> Statement:
        """Route to the correct statement parser based on the first token."""
        token = self._current()

        if token.type == TokenType.SELECT:
            return self._parse_select()
        elif token.type == TokenType.INSERT:
            return self._parse_insert()
        elif token.type == TokenType.DELETE:
            return self._parse_delete()
        elif token.type == TokenType.UPDATE:
            return self._parse_update()
        elif token.type == TokenType.CREATE:
            return self._parse_create()
        elif token.type == TokenType.DROP:
            return self._parse_drop()
        elif token.type == TokenType.BEGIN:
            self._advance()
            self._match(TokenType.TRANSACTION)
            return BeginStatement()
        elif token.type == TokenType.COMMIT:
            self._advance()
            return CommitStatement()
        elif token.type == TokenType.ROLLBACK:
            self._advance()
            return RollbackStatement()
        else:
            raise ParseError(f"Unexpected token: {token}")

    # ─── SELECT ──────────────────────────────────────────────────────

    def _parse_select(self) -> SelectStatement:
        self._expect(TokenType.SELECT)

        stmt = SelectStatement()

        # DISTINCT
        if self._match(TokenType.DISTINCT):
            stmt.distinct = True

        # Column list
        stmt.columns = self._parse_select_list()

        # FROM
        if self._match(TokenType.FROM):
            stmt.from_table = self._parse_table_ref()

            # JOINs
            while self._check(TokenType.JOIN, TokenType.INNER, TokenType.LEFT,
                               TokenType.RIGHT, TokenType.CROSS):
                stmt.joins.append(self._parse_join())

        # WHERE
        if self._match(TokenType.WHERE):
            stmt.where = self._parse_expression()

        # GROUP BY
        if self._check(TokenType.GROUP):
            self._advance()
            self._expect(TokenType.BY)
            stmt.group_by = self._parse_expression_list()

        # HAVING
        if self._match(TokenType.HAVING):
            stmt.having = self._parse_expression()

        # ORDER BY
        if self._check(TokenType.ORDER):
            self._advance()
            self._expect(TokenType.BY)
            stmt.order_by = self._parse_order_by_list()

        # LIMIT
        if self._match(TokenType.LIMIT):
            token = self._expect(TokenType.INTEGER_LITERAL)
            stmt.limit = int(token.value)

        return stmt

    def _parse_select_list(self) -> List[SelectItem]:
        """Parse the SELECT column list."""
        items = []

        item = self._parse_select_item()
        items.append(item)

        while self._match(TokenType.COMMA):
            items.append(self._parse_select_item())

        return items

    def _parse_select_item(self) -> SelectItem:
        """Parse a single SELECT item (expression [AS alias])."""
        if self._check(TokenType.STAR):
            self._advance()
            return SelectItem(expr=StarExpr())

        expr = self._parse_expression()

        alias = None
        if self._match(TokenType.AS):
            alias = self._expect(TokenType.IDENTIFIER).value
        elif self._check(TokenType.IDENTIFIER) and not self._check(
                TokenType.FROM, TokenType.WHERE, TokenType.JOIN,
                TokenType.ORDER, TokenType.GROUP, TokenType.LIMIT):
            # Implicit alias (without AS)
            alias = self._advance().value

        return SelectItem(expr=expr, alias=alias)

    def _parse_table_ref(self) -> TableRef:
        """Parse a table reference with optional alias."""
        name = self._expect(TokenType.IDENTIFIER).value
        alias = None

        if self._match(TokenType.AS):
            alias = self._expect(TokenType.IDENTIFIER).value
        elif self._check(TokenType.IDENTIFIER):
            # Check it's not a keyword being used as next clause
            next_val = self._current().value.upper()
            if next_val not in ('WHERE', 'JOIN', 'INNER', 'LEFT', 'RIGHT',
                                'CROSS', 'ON', 'ORDER', 'GROUP', 'LIMIT',
                                'HAVING', 'SET'):
                alias = self._advance().value

        return TableRef(table_name=name, alias=alias)

    def _parse_join(self) -> JoinClause:
        """Parse a JOIN clause."""
        join_type = 'INNER'

        if self._match(TokenType.INNER):
            join_type = 'INNER'
        elif self._match(TokenType.LEFT):
            join_type = 'LEFT'
        elif self._match(TokenType.RIGHT):
            join_type = 'RIGHT'
        elif self._match(TokenType.CROSS):
            join_type = 'CROSS'

        self._expect(TokenType.JOIN)

        table = self._parse_table_ref()
        condition = None

        if join_type != 'CROSS' and self._match(TokenType.ON):
            condition = self._parse_expression()

        return JoinClause(join_type=join_type, table=table, condition=condition)

    def _parse_order_by_list(self) -> List[OrderByItem]:
        """Parse ORDER BY list."""
        items = []
        items.append(self._parse_order_by_item())
        while self._match(TokenType.COMMA):
            items.append(self._parse_order_by_item())
        return items

    def _parse_order_by_item(self) -> OrderByItem:
        expr = self._parse_expression()
        ascending = True
        if self._match(TokenType.ASC):
            ascending = True
        elif self._match(TokenType.DESC):
            ascending = False
        return OrderByItem(expr=expr, ascending=ascending)

    # ─── INSERT ──────────────────────────────────────────────────────

    def _parse_insert(self) -> InsertStatement:
        self._expect(TokenType.INSERT)
        self._expect(TokenType.INTO)

        stmt = InsertStatement()
        stmt.table_name = self._expect(TokenType.IDENTIFIER).value

        # Optional column list
        if self._match(TokenType.LPAREN):
            stmt.columns = []
            stmt.columns.append(self._expect(TokenType.IDENTIFIER).value)
            while self._match(TokenType.COMMA):
                stmt.columns.append(self._expect(TokenType.IDENTIFIER).value)
            self._expect(TokenType.RPAREN)

        # VALUES
        self._expect(TokenType.VALUES)

        # Multiple value rows
        stmt.values = []
        stmt.values.append(self._parse_value_row())
        while self._match(TokenType.COMMA):
            stmt.values.append(self._parse_value_row())

        return stmt

    def _parse_value_row(self) -> List[Expression]:
        """Parse a single row of values: (val1, val2, ...)."""
        self._expect(TokenType.LPAREN)
        values = [self._parse_expression()]
        while self._match(TokenType.COMMA):
            values.append(self._parse_expression())
        self._expect(TokenType.RPAREN)
        return values

    # ─── DELETE ──────────────────────────────────────────────────────

    def _parse_delete(self) -> DeleteStatement:
        self._expect(TokenType.DELETE)
        self._expect(TokenType.FROM)

        stmt = DeleteStatement()
        stmt.table_name = self._expect(TokenType.IDENTIFIER).value

        if self._match(TokenType.WHERE):
            stmt.where = self._parse_expression()

        return stmt

    # ─── UPDATE ──────────────────────────────────────────────────────

    def _parse_update(self) -> UpdateStatement:
        self._expect(TokenType.UPDATE)

        stmt = UpdateStatement()
        stmt.table_name = self._expect(TokenType.IDENTIFIER).value

        self._expect(TokenType.SET)

        # Assignments
        stmt.assignments = []
        col = self._expect(TokenType.IDENTIFIER).value
        self._expect(TokenType.EQUALS)
        val = self._parse_expression()
        stmt.assignments.append((col, val))

        while self._match(TokenType.COMMA):
            col = self._expect(TokenType.IDENTIFIER).value
            self._expect(TokenType.EQUALS)
            val = self._parse_expression()
            stmt.assignments.append((col, val))

        if self._match(TokenType.WHERE):
            stmt.where = self._parse_expression()

        return stmt

    # ─── CREATE ──────────────────────────────────────────────────────

    def _parse_create(self) -> Statement:
        self._expect(TokenType.CREATE)

        if self._check(TokenType.TABLE):
            return self._parse_create_table()
        elif self._check(TokenType.INDEX):
            return self._parse_create_index()
        else:
            raise ParseError(f"Expected TABLE or INDEX after CREATE, got {self._current()}")

    def _parse_create_table(self) -> CreateTableStatement:
        self._expect(TokenType.TABLE)

        stmt = CreateTableStatement()
        stmt.table_name = self._expect(TokenType.IDENTIFIER).value

        self._expect(TokenType.LPAREN)

        # Column definitions
        stmt.columns = []
        stmt.columns.append(self._parse_column_def())
        while self._match(TokenType.COMMA):
            stmt.columns.append(self._parse_column_def())

        self._expect(TokenType.RPAREN)
        return stmt

    def _parse_column_def(self) -> dict:
        """Parse a column definition: name TYPE [PRIMARY KEY] [NOT NULL]."""
        col = {
            'name': self._expect(TokenType.IDENTIFIER).value,
            'type': self._parse_data_type(),
            'primary_key': False,
            'nullable': True,
        }

        # Optional constraints
        while True:
            if self._check(TokenType.PRIMARY):
                self._advance()
                self._expect(TokenType.KEY)
                col['primary_key'] = True
                col['nullable'] = False
            elif self._check(TokenType.NOT):
                self._advance()
                self._expect(TokenType.NULL)
                col['nullable'] = False
            else:
                break

        return col

    def _parse_data_type(self) -> str:
        """Parse a column data type."""
        type_tokens = [
            TokenType.INTEGER_TYPE, TokenType.FLOAT_TYPE,
            TokenType.VARCHAR_TYPE, TokenType.BOOLEAN_TYPE,
            TokenType.TEXT_TYPE,
        ]
        token = self._expect(*type_tokens)
        type_name = {
            TokenType.INTEGER_TYPE: 'INTEGER',
            TokenType.FLOAT_TYPE: 'FLOAT',
            TokenType.VARCHAR_TYPE: 'VARCHAR',
            TokenType.BOOLEAN_TYPE: 'BOOLEAN',
            TokenType.TEXT_TYPE: 'TEXT',
        }[token.type]

        # Optional length: VARCHAR(255)
        if self._match(TokenType.LPAREN):
            self._expect(TokenType.INTEGER_LITERAL)
            self._expect(TokenType.RPAREN)

        return type_name

    def _parse_create_index(self) -> CreateIndexStatement:
        self._expect(TokenType.INDEX)

        stmt = CreateIndexStatement()
        stmt.index_name = self._expect(TokenType.IDENTIFIER).value

        self._expect(TokenType.ON)
        stmt.table_name = self._expect(TokenType.IDENTIFIER).value

        self._expect(TokenType.LPAREN)
        stmt.column_name = self._expect(TokenType.IDENTIFIER).value
        self._expect(TokenType.RPAREN)

        return stmt

    # ─── DROP ────────────────────────────────────────────────────────

    def _parse_drop(self) -> DropTableStatement:
        self._expect(TokenType.DROP)
        self._expect(TokenType.TABLE)

        stmt = DropTableStatement()
        stmt.table_name = self._expect(TokenType.IDENTIFIER).value
        return stmt

    # ─── Expression parsing (precedence climbing) ────────────────────

    def _parse_expression(self) -> Expression:
        """Parse an expression (entry point)."""
        return self._parse_or()

    def _parse_or(self) -> Expression:
        left = self._parse_and()
        while self._match(TokenType.OR):
            right = self._parse_and()
            left = BinaryOp(left=left, op='OR', right=right)
        return left

    def _parse_and(self) -> Expression:
        left = self._parse_not()
        while self._match(TokenType.AND):
            right = self._parse_not()
            left = BinaryOp(left=left, op='AND', right=right)
        return left

    def _parse_not(self) -> Expression:
        if self._match(TokenType.NOT):
            operand = self._parse_not()
            return UnaryOp(op='NOT', operand=operand)
        return self._parse_comparison()

    def _parse_comparison(self) -> Expression:
        left = self._parse_addition()

        comp_ops = [
            TokenType.EQUALS, TokenType.NOT_EQUALS,
            TokenType.LESS_THAN, TokenType.GREATER_THAN,
            TokenType.LESS_EQUAL, TokenType.GREATER_EQUAL,
        ]

        if self._check(*comp_ops):
            op_token = self._advance()
            right = self._parse_addition()
            return BinaryOp(left=left, op=op_token.value, right=right)

        # IS NULL / IS NOT NULL
        if self._check(TokenType.IS):
            self._advance()
            negated = False
            if self._match(TokenType.NOT):
                negated = True
            self._expect(TokenType.NULL)
            return IsNullExpr(expr=left, negated=negated)

        # BETWEEN
        if self._check(TokenType.BETWEEN):
            self._advance()
            low = self._parse_addition()
            self._expect(TokenType.AND)
            high = self._parse_addition()
            return BetweenExpr(expr=left, low=low, high=high)

        # IN
        if self._check(TokenType.IN):
            self._advance()
            self._expect(TokenType.LPAREN)
            vals = [self._parse_expression()]
            while self._match(TokenType.COMMA):
                vals.append(self._parse_expression())
            self._expect(TokenType.RPAREN)
            return InExpr(expr=left, values=vals)

        # LIKE
        if self._check(TokenType.LIKE):
            self._advance()
            pattern = self._parse_primary()
            return BinaryOp(left=left, op='LIKE', right=pattern)

        return left

    def _parse_addition(self) -> Expression:
        left = self._parse_multiplication()
        while self._check(TokenType.PLUS, TokenType.MINUS):
            op_token = self._advance()
            right = self._parse_multiplication()
            left = BinaryOp(left=left, op=op_token.value, right=right)
        return left

    def _parse_multiplication(self) -> Expression:
        left = self._parse_unary()
        while self._check(TokenType.STAR, TokenType.DIVIDE):
            op_token = self._advance()
            right = self._parse_unary()
            left = BinaryOp(left=left, op=op_token.value, right=right)
        return left

    def _parse_unary(self) -> Expression:
        if self._match(TokenType.MINUS):
            operand = self._parse_primary()
            return UnaryOp(op='-', operand=operand)
        return self._parse_primary()

    def _parse_primary(self) -> Expression:
        """Parse a primary expression (literal, column ref, function, parens)."""
        token = self._current()

        # Parenthesized expression
        if token.type == TokenType.LPAREN:
            self._advance()
            expr = self._parse_expression()
            self._expect(TokenType.RPAREN)
            return expr

        # NULL literal
        if token.type == TokenType.NULL:
            self._advance()
            return Literal(value=None, data_type='NULL')

        # Boolean literals
        if token.type == TokenType.TRUE:
            self._advance()
            return Literal(value=True, data_type='BOOLEAN')
        if token.type == TokenType.FALSE:
            self._advance()
            return Literal(value=False, data_type='BOOLEAN')

        # Integer literal
        if token.type == TokenType.INTEGER_LITERAL:
            self._advance()
            return Literal(value=int(token.value), data_type='INTEGER')

        # Float literal
        if token.type == TokenType.FLOAT_LITERAL:
            self._advance()
            return Literal(value=float(token.value), data_type='FLOAT')

        # String literal
        if token.type == TokenType.STRING_LITERAL:
            self._advance()
            return Literal(value=token.value, data_type='STRING')

        # Aggregate functions
        if token.type in (TokenType.COUNT, TokenType.SUM, TokenType.AVG,
                          TokenType.MIN, TokenType.MAX):
            return self._parse_function()

        # Star (for SELECT *)
        if token.type == TokenType.STAR:
            self._advance()
            return StarExpr()

        # Identifier (column ref, possibly table.column)
        if token.type == TokenType.IDENTIFIER:
            name = self._advance().value
            if self._match(TokenType.DOT):
                # table.column
                if self._check(TokenType.STAR):
                    self._advance()
                    return StarExpr(table=name)
                col = self._expect(TokenType.IDENTIFIER).value
                return ColumnRef(column=col, table=name)
            # Check for function call syntax: name(...)
            if self._check(TokenType.LPAREN):
                return self._parse_function_call(name)
            return ColumnRef(column=name)

        raise ParseError(f"Unexpected token in expression: {token}")

    def _parse_function(self) -> FunctionCall:
        """Parse an aggregate function: COUNT(*), SUM(col), etc."""
        name = self._advance().value.upper()
        self._expect(TokenType.LPAREN)

        distinct = False
        if self._match(TokenType.DISTINCT):
            distinct = True

        args = []
        if self._check(TokenType.STAR):
            args.append(StarExpr())
            self._advance()
        elif not self._check(TokenType.RPAREN):
            args.append(self._parse_expression())
            while self._match(TokenType.COMMA):
                args.append(self._parse_expression())

        self._expect(TokenType.RPAREN)
        return FunctionCall(name=name, args=args, distinct=distinct)

    def _parse_function_call(self, name: str) -> FunctionCall:
        """Parse a generic function call: name(args...)."""
        self._expect(TokenType.LPAREN)
        args = []
        if not self._check(TokenType.RPAREN):
            args.append(self._parse_expression())
            while self._match(TokenType.COMMA):
                args.append(self._parse_expression())
        self._expect(TokenType.RPAREN)
        return FunctionCall(name=name.upper(), args=args)

    def _parse_expression_list(self) -> List[Expression]:
        """Parse a comma-separated list of expressions."""
        exprs = [self._parse_expression()]
        while self._match(TokenType.COMMA):
            exprs.append(self._parse_expression())
        return exprs
