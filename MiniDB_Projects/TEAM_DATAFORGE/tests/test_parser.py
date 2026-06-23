"""
Tests for SQL Parser — Lexer, Parser, and AST nodes.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from src.parser.lexer import Lexer, TokenType
from src.parser.parser import Parser, ParseError
from src.parser.ast_nodes import *


class TestLexer(unittest.TestCase):
    def test_select_tokens(self):
        lexer = Lexer("SELECT * FROM employees")
        tokens = lexer.tokenize()
        types = [t.type for t in tokens]
        self.assertEqual(types, [
            TokenType.SELECT, TokenType.STAR, TokenType.FROM,
            TokenType.IDENTIFIER, TokenType.EOF
        ])

    def test_string_literal(self):
        lexer = Lexer("'hello world'")
        tokens = lexer.tokenize()
        self.assertEqual(tokens[0].type, TokenType.STRING_LITERAL)
        self.assertEqual(tokens[0].value, 'hello world')

    def test_numbers(self):
        lexer = Lexer("42 3.14")
        tokens = lexer.tokenize()
        self.assertEqual(tokens[0].type, TokenType.INTEGER_LITERAL)
        self.assertEqual(tokens[1].type, TokenType.FLOAT_LITERAL)

    def test_operators(self):
        lexer = Lexer("= != < > <= >= <>")
        tokens = lexer.tokenize()
        types = [t.type for t in tokens[:-1]]  # Exclude EOF
        self.assertEqual(types, [
            TokenType.EQUALS, TokenType.NOT_EQUALS,
            TokenType.LESS_THAN, TokenType.GREATER_THAN,
            TokenType.LESS_EQUAL, TokenType.GREATER_EQUAL,
            TokenType.NOT_EQUALS,
        ])

    def test_keywords(self):
        lexer = Lexer("INSERT INTO VALUES DELETE WHERE JOIN")
        tokens = lexer.tokenize()
        types = [t.type for t in tokens[:-1]]
        self.assertEqual(types, [
            TokenType.INSERT, TokenType.INTO, TokenType.VALUES,
            TokenType.DELETE, TokenType.WHERE, TokenType.JOIN,
        ])


class TestParser(unittest.TestCase):
    def test_select_star(self):
        ast = Parser("SELECT * FROM employees").parse()
        self.assertIsInstance(ast, SelectStatement)
        self.assertEqual(ast.from_table.table_name, 'employees')

    def test_select_columns(self):
        ast = Parser("SELECT name, age FROM employees").parse()
        self.assertIsInstance(ast, SelectStatement)
        self.assertEqual(len(ast.columns), 2)

    def test_select_where(self):
        ast = Parser("SELECT * FROM employees WHERE age > 30").parse()
        self.assertIsNotNone(ast.where)
        self.assertIsInstance(ast.where, BinaryOp)
        self.assertEqual(ast.where.op, '>')

    def test_select_join(self):
        ast = Parser(
            "SELECT * FROM employees JOIN departments ON employees.dept_id = departments.id"
        ).parse()
        self.assertEqual(len(ast.joins), 1)
        self.assertEqual(ast.joins[0].join_type, 'INNER')

    def test_select_order_by_limit(self):
        ast = Parser("SELECT * FROM employees ORDER BY salary DESC LIMIT 10").parse()
        self.assertEqual(len(ast.order_by), 1)
        self.assertFalse(ast.order_by[0].ascending)
        self.assertEqual(ast.limit, 10)

    def test_insert(self):
        ast = Parser(
            "INSERT INTO employees (id, name) VALUES (1, 'Alice')"
        ).parse()
        self.assertIsInstance(ast, InsertStatement)
        self.assertEqual(ast.table_name, 'employees')
        self.assertEqual(len(ast.columns), 2)
        self.assertEqual(len(ast.values), 1)

    def test_insert_multiple_rows(self):
        ast = Parser(
            "INSERT INTO t (a, b) VALUES (1, 2), (3, 4), (5, 6)"
        ).parse()
        self.assertEqual(len(ast.values), 3)

    def test_delete(self):
        ast = Parser("DELETE FROM employees WHERE id = 5").parse()
        self.assertIsInstance(ast, DeleteStatement)
        self.assertEqual(ast.table_name, 'employees')
        self.assertIsNotNone(ast.where)

    def test_create_table(self):
        ast = Parser(
            "CREATE TABLE employees (id INTEGER PRIMARY KEY, name VARCHAR NOT NULL, salary FLOAT)"
        ).parse()
        self.assertIsInstance(ast, CreateTableStatement)
        self.assertEqual(ast.table_name, 'employees')
        self.assertEqual(len(ast.columns), 3)
        self.assertTrue(ast.columns[0]['primary_key'])
        self.assertFalse(ast.columns[1]['nullable'])

    def test_update(self):
        ast = Parser(
            "UPDATE employees SET salary = 50000 WHERE id = 1"
        ).parse()
        self.assertIsInstance(ast, UpdateStatement)
        self.assertEqual(len(ast.assignments), 1)

    def test_begin_commit_rollback(self):
        self.assertIsInstance(Parser("BEGIN").parse(), BeginStatement)
        self.assertIsInstance(Parser("COMMIT").parse(), CommitStatement)
        self.assertIsInstance(Parser("ROLLBACK").parse(), RollbackStatement)

    def test_select_with_aggregate(self):
        ast = Parser("SELECT COUNT(*), AVG(salary) FROM employees").parse()
        self.assertEqual(len(ast.columns), 2)
        self.assertIsInstance(ast.columns[0].expr, FunctionCall)
        self.assertEqual(ast.columns[0].expr.name, 'COUNT')

    def test_invalid_sql(self):
        with self.assertRaises(ParseError):
            Parser("INVALID QUERY").parse()

    def test_create_index(self):
        ast = Parser("CREATE INDEX idx_name ON employees (name)").parse()
        self.assertIsInstance(ast, CreateIndexStatement)
        self.assertEqual(ast.index_name, 'idx_name')
        self.assertEqual(ast.table_name, 'employees')


if __name__ == '__main__':
    unittest.main()
