"""
SQL Lexer — Tokenizer for MiniDB's SQL dialect.

Converts a raw SQL string into a stream of tokens for the parser.
Supports keywords, identifiers, numeric/string literals, and operators.
"""

from enum import Enum, auto
from dataclasses import dataclass
from typing import List


class TokenType(Enum):
    """All token types recognized by the lexer."""
    # Keywords
    SELECT = auto()
    FROM = auto()
    WHERE = auto()
    INSERT = auto()
    INTO = auto()
    VALUES = auto()
    DELETE = auto()
    UPDATE = auto()
    SET = auto()
    CREATE = auto()
    TABLE = auto()
    DROP = auto()
    INDEX = auto()
    ON = auto()
    JOIN = auto()
    INNER = auto()
    LEFT = auto()
    RIGHT = auto()
    CROSS = auto()
    AND = auto()
    OR = auto()
    NOT = auto()
    IN = auto()
    IS = auto()
    NULL = auto()
    AS = auto()
    ORDER = auto()
    BY = auto()
    ASC = auto()
    DESC = auto()
    LIMIT = auto()
    PRIMARY = auto()
    KEY = auto()
    INTEGER_TYPE = auto()
    FLOAT_TYPE = auto()
    VARCHAR_TYPE = auto()
    BOOLEAN_TYPE = auto()
    TEXT_TYPE = auto()
    TRUE = auto()
    FALSE = auto()
    BEGIN = auto()
    COMMIT = auto()
    ROLLBACK = auto()
    TRANSACTION = auto()
    GROUP = auto()
    HAVING = auto()
    COUNT = auto()
    SUM = auto()
    AVG = auto()
    MIN = auto()
    MAX = auto()
    BETWEEN = auto()
    LIKE = auto()
    EXISTS = auto()
    DISTINCT = auto()

    # Identifiers and literals
    IDENTIFIER = auto()
    INTEGER_LITERAL = auto()
    FLOAT_LITERAL = auto()
    STRING_LITERAL = auto()

    # Operators
    STAR = auto()           # *
    PLUS = auto()           # +
    MINUS = auto()          # -
    DIVIDE = auto()         # /
    EQUALS = auto()         # =
    NOT_EQUALS = auto()     # != or <>
    LESS_THAN = auto()      # <
    GREATER_THAN = auto()   # >
    LESS_EQUAL = auto()     # <=
    GREATER_EQUAL = auto()  # >=

    # Delimiters
    LPAREN = auto()         # (
    RPAREN = auto()         # )
    COMMA = auto()          # ,
    SEMICOLON = auto()      # ;
    DOT = auto()            # .

    # Special
    EOF = auto()


# Map keyword strings to token types
KEYWORDS = {
    'SELECT': TokenType.SELECT,
    'FROM': TokenType.FROM,
    'WHERE': TokenType.WHERE,
    'INSERT': TokenType.INSERT,
    'INTO': TokenType.INTO,
    'VALUES': TokenType.VALUES,
    'DELETE': TokenType.DELETE,
    'UPDATE': TokenType.UPDATE,
    'SET': TokenType.SET,
    'CREATE': TokenType.CREATE,
    'TABLE': TokenType.TABLE,
    'DROP': TokenType.DROP,
    'INDEX': TokenType.INDEX,
    'ON': TokenType.ON,
    'JOIN': TokenType.JOIN,
    'INNER': TokenType.INNER,
    'LEFT': TokenType.LEFT,
    'RIGHT': TokenType.RIGHT,
    'CROSS': TokenType.CROSS,
    'AND': TokenType.AND,
    'OR': TokenType.OR,
    'NOT': TokenType.NOT,
    'IN': TokenType.IN,
    'IS': TokenType.IS,
    'NULL': TokenType.NULL,
    'AS': TokenType.AS,
    'ORDER': TokenType.ORDER,
    'BY': TokenType.BY,
    'ASC': TokenType.ASC,
    'DESC': TokenType.DESC,
    'LIMIT': TokenType.LIMIT,
    'PRIMARY': TokenType.PRIMARY,
    'KEY': TokenType.KEY,
    'INTEGER': TokenType.INTEGER_TYPE,
    'INT': TokenType.INTEGER_TYPE,
    'FLOAT': TokenType.FLOAT_TYPE,
    'REAL': TokenType.FLOAT_TYPE,
    'VARCHAR': TokenType.VARCHAR_TYPE,
    'BOOLEAN': TokenType.BOOLEAN_TYPE,
    'BOOL': TokenType.BOOLEAN_TYPE,
    'TEXT': TokenType.TEXT_TYPE,
    'TRUE': TokenType.TRUE,
    'FALSE': TokenType.FALSE,
    'BEGIN': TokenType.BEGIN,
    'COMMIT': TokenType.COMMIT,
    'ROLLBACK': TokenType.ROLLBACK,
    'TRANSACTION': TokenType.TRANSACTION,
    'GROUP': TokenType.GROUP,
    'HAVING': TokenType.HAVING,
    'COUNT': TokenType.COUNT,
    'SUM': TokenType.SUM,
    'AVG': TokenType.AVG,
    'MIN': TokenType.MIN,
    'MAX': TokenType.MAX,
    'BETWEEN': TokenType.BETWEEN,
    'LIKE': TokenType.LIKE,
    'EXISTS': TokenType.EXISTS,
    'DISTINCT': TokenType.DISTINCT,
}


@dataclass
class Token:
    """A single token produced by the lexer."""
    type: TokenType
    value: str
    position: int = 0   # Character position in input

    def __repr__(self):
        return f"Token({self.type.name}, {self.value!r})"


class LexerError(Exception):
    """Error during lexical analysis."""
    pass


class Lexer:
    """
    SQL Lexer — converts a SQL string into a list of tokens.

    Usage:
        lexer = Lexer("SELECT * FROM employees WHERE id = 1")
        tokens = lexer.tokenize()
    """

    def __init__(self, text: str):
        self.text = text
        self.pos = 0
        self.length = len(text)

    def tokenize(self) -> List[Token]:
        """
        Tokenize the input SQL string.

        Returns:
            List of Token objects.
        """
        tokens = []
        while self.pos < self.length:
            self._skip_whitespace()
            if self.pos >= self.length:
                break

            ch = self.text[self.pos]

            # Single-line comment
            if ch == '-' and self.pos + 1 < self.length and self.text[self.pos + 1] == '-':
                self._skip_line_comment()
                continue

            # String literal
            if ch in ("'", '"'):
                tokens.append(self._read_string())
                continue

            # Number
            if ch.isdigit() or (ch == '-' and self._peek_digit()):
                tokens.append(self._read_number())
                continue

            # Identifier or keyword
            if ch.isalpha() or ch == '_':
                tokens.append(self._read_identifier())
                continue

            # Operators and delimiters
            token = self._read_operator()
            if token:
                tokens.append(token)
                continue

            raise LexerError(f"Unexpected character '{ch}' at position {self.pos}")

        tokens.append(Token(TokenType.EOF, '', self.pos))
        return tokens

    def _skip_whitespace(self):
        while self.pos < self.length and self.text[self.pos] in (' ', '\t', '\n', '\r'):
            self.pos += 1

    def _skip_line_comment(self):
        while self.pos < self.length and self.text[self.pos] != '\n':
            self.pos += 1

    def _peek_digit(self) -> bool:
        return self.pos + 1 < self.length and self.text[self.pos + 1].isdigit()

    def _read_string(self) -> Token:
        """Read a string literal ('...' or "...")."""
        start = self.pos
        quote = self.text[self.pos]
        self.pos += 1
        result = []

        while self.pos < self.length:
            ch = self.text[self.pos]
            if ch == quote:
                if self.pos + 1 < self.length and self.text[self.pos + 1] == quote:
                    result.append(quote)
                    self.pos += 2
                else:
                    self.pos += 1
                    return Token(TokenType.STRING_LITERAL, ''.join(result), start)
            else:
                result.append(ch)
                self.pos += 1

        raise LexerError(f"Unterminated string starting at position {start}")

    def _read_number(self) -> Token:
        """Read an integer or float literal."""
        start = self.pos
        is_float = False

        if self.text[self.pos] == '-':
            self.pos += 1

        while self.pos < self.length and self.text[self.pos].isdigit():
            self.pos += 1

        if self.pos < self.length and self.text[self.pos] == '.':
            is_float = True
            self.pos += 1
            while self.pos < self.length and self.text[self.pos].isdigit():
                self.pos += 1

        value = self.text[start:self.pos]
        if is_float:
            return Token(TokenType.FLOAT_LITERAL, value, start)
        return Token(TokenType.INTEGER_LITERAL, value, start)

    def _read_identifier(self) -> Token:
        """Read an identifier or keyword."""
        start = self.pos
        while self.pos < self.length and (self.text[self.pos].isalnum() or self.text[self.pos] == '_'):
            self.pos += 1

        value = self.text[start:self.pos]
        upper = value.upper()

        # Check if it's a keyword
        if upper in KEYWORDS:
            return Token(KEYWORDS[upper], value, start)

        return Token(TokenType.IDENTIFIER, value, start)

    def _read_operator(self) -> Token:
        """Read an operator or delimiter."""
        start = self.pos
        ch = self.text[self.pos]

        # Two-character operators
        if self.pos + 1 < self.length:
            two = self.text[self.pos:self.pos + 2]
            if two == '!=':
                self.pos += 2
                return Token(TokenType.NOT_EQUALS, '!=', start)
            if two == '<>':
                self.pos += 2
                return Token(TokenType.NOT_EQUALS, '<>', start)
            if two == '<=':
                self.pos += 2
                return Token(TokenType.LESS_EQUAL, '<=', start)
            if two == '>=':
                self.pos += 2
                return Token(TokenType.GREATER_EQUAL, '>=', start)

        # Single-character operators
        single_ops = {
            '*': TokenType.STAR,
            '+': TokenType.PLUS,
            '-': TokenType.MINUS,
            '/': TokenType.DIVIDE,
            '=': TokenType.EQUALS,
            '<': TokenType.LESS_THAN,
            '>': TokenType.GREATER_THAN,
            '(': TokenType.LPAREN,
            ')': TokenType.RPAREN,
            ',': TokenType.COMMA,
            ';': TokenType.SEMICOLON,
            '.': TokenType.DOT,
        }

        if ch in single_ops:
            self.pos += 1
            return Token(single_ops[ch], ch, start)

        return None
