# Advanced DBMS Lab 7: Recursive-Descent Query Parser

> Author: Krritin Keshan (24BCS10122)
> Course: Advanced Database Management Systems
> Language: C++17

## Included Files

| File | Description |
|------|-------------|
| main.cpp | Lexer, Parser, AST definition, Evaluator and execution driver. |
| makefile | Build script configured with C++17 flags. |
| readme.md | This documentation. |

## Compilation & Execution

make          # Compile
make run      # Compile and run
make clean    # Remove binary

## The Concept

A WHERE clause like `marks >= 80 AND (age < 20 OR id = 5)` has precedence rules.
Instead of a precedence table, this approach bakes precedence into the grammar:

query  := SELECT Name FROM Name WHERE expr
expr   := term  (OR  term)*       # OR loosest
term   := factor (AND factor)*    # AND tighter
factor := '(' expr ')' | Name Cmp Int   # comparisons tightest

## Architecture

1. Lexer: tokenizes the SQL string into keywords, identifiers, numbers, operators.
2. Parser: recursive descent — each OR/AND becomes a BinOp node; each comparison becomes a BinOp over Column and Number.
3. AST: single tagged Expr struct (Column / Number / BinOp), no virtual hierarchy.
4. Evaluator: recursive tree walk; AND/OR recurse on both sides, comparisons look up column values per row.

## Sample Output

Query: SELECT name FROM students WHERE marks >= 80 AND (age < 20 OR id = 5)

WHERE as an AST (precedence baked into the tree):
AND
  >=
    marks
    80
  OR
    
      age
      20
    =
      id
      5

Matching rows:
  Priya
  Meera

## Versus Shunting-Yard

| Feature | queryparsing | shunting_yard |
|---|---|---|
| Parse output | AST (tree) | Flat RPN list |
| Precedence lives in | Grammar rule order | Precedence number table |
| Evaluator | Recursive tree walk | One-pass integer stack |