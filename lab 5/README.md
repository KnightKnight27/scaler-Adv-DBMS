# Lab Session 5: Shunting-Yard Algorithm + Minimal SQL SELECT Parser over vector<Row>

## Objective
1. Implement Dijkstra's Shunting-Yard algorithm to evaluate infix arithmetic/boolean expressions (used in SQL WHERE clause evaluation).
2. Build a minimal SQL parser that handles SELECT queries and executes them against an already-fetched `vector<Row>` in memory.

---

## Architecture Overview

```
SQL Query String
   │
Token Stream (Lexer)     ◄── tokenize()
   │
Parser                   ◄── parse_select()  produces SelectQuery AST
   │
Query Executor           ◄── execute()
   ├── WHERE filter      ◄── evaluates row values via to_rpn() & eval_rpn()
   ├── Project columns
   ├── ORDER BY sort
   └── LIMIT limit
   │
Result Set (`std::vector<Row>`)
```

---

## Files

| File | Description |
|------|-------------|
| [sql_parser.cpp](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%205/sql_parser.cpp) | Full implementation containing tokenizer, RPN converter, RPN evaluator, SQL parser, executor, and demonstration. |
| [Makefile](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%205/Makefile) | Makefile for compilation and execution. |

---

## Features & Enhancements

### Bug Fixes in the Parser
In the original parser proposal, using a nested `while (ss >> w2)` block with a `goto` statement caused the outer loop variable `word` to be advanced incorrectly. In this implementation, the query string is split into discrete word tokens beforehand, allowing safe look-aheads and look-behinds, preventing clauses like `ORDER BY` and `LIMIT` from being skipped.

### Supported Operators
The Shunting-Yard evaluator supports:
- Logical: `&&` (AND), `||` (OR)
- Comparison: `<`, `>`, `<=`, `>=`, `=`, `!=`
- Arithmetic: `+`, `-`, `*`, `/`, `^` (exponentiation, right-associative)

---

## Compilation and Running

You can compile and run the project using the helper `Makefile`:

```bash
# To compile and run
make run

# To clean the build binary
make clean
```

Or run manually:

```bash
g++ -std=c++17 -o sql_parser sql_parser.cpp && ./sql_parser
```
