# Lab 7: Shunting-Yard Algorithm + SQL Parser

## Overview
A small in-memory SQL-like engine with:
- **Shunting-Yard Algorithm** for expression evaluation
- **Recursive Descent Parser** for `SELECT` queries
- **Query Executor** for filtering, projection, ordering, and limiting

## Folder Structure
```txt
app/
├── types.h
├── expressions.h
├── lexer.h / lexer.cpp
├── parser.h
├── executor.h
├── main.cpp
└── Makefile
```

## Build
```bash
g++ -std=c++17 -Wall -Wextra -O2 -o sql_parser main.cpp lexer.cpp
./sql_parser
```

## What it does
The program supports queries like:

```sql
SELECT name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3
```

It also understands boolean operators like `&&`, `||`, `AND`, and `OR` in `WHERE`.

## Files
- `types.h`: basic token and row types
- `lexer.h/cpp`: turns SQL text into tokens
- `parser.h`: reads tokens and builds a query object
- `executor.h`: evaluates expressions and runs the query
- `main.cpp`: demo program
