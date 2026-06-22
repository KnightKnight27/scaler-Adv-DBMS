
# Lab 6 - Minimal SQL Engine using Shunting-Yard Algorithm

## Objective
Build a lightweight SQL execution engine in C++17 that uses Dijkstra's Shunting-Yard algorithm to parse and evaluate expressions used in WHERE clauses.

## Design Overview
Components:
- Token / Lexer
- ExpressionParser (Infix -> Postfix)
- ExpressionEvaluator
- SQLParser
- QueryExecutor
- Record / Table

## Execution Flow
SQL Query
-> Parse Query
-> Convert WHERE expression to postfix
-> Evaluate against rows
-> Projection
-> ORDER BY
-> LIMIT
-> Tabular Output

## Shunting-Yard
The parser converts infix expressions into postfix notation using operator precedence and associativity. Postfix expressions are then evaluated using a stack.

## Supported SQL
- SELECT *
- Column Projection
- WHERE
- ORDER BY ASC/DESC
- LIMIT

## Compile

```bash
g++ -std=c++17 sql_parser.cpp -o sql_parser
```

## Run

```bash
./sql_parser
```
