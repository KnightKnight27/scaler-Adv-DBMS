# Lab Session 5: Shunting-Yard Algorithm + Minimal SQL SELECT Parser

**Name:** Snehangshu Roy
**Roll No:** 24BCS10155

## Objective
1. Implement Dijkstra's Shunting-Yard algorithm to evaluate infix
   arithmetic/boolean expressions (the mechanism behind SQL WHERE evaluation).
2. Build a minimal SQL parser that handles `SELECT` queries and executes them
   against an already-fetched `vector<Row>` in memory.

## Files
- `sql_parser.cpp` — tokenizer, shunting-yard (infix→RPN), RPN evaluator,
  `SELECT` parser, and executor; combined into one runnable program.
- `makefile` — build / run.

## Build & Run
```bash
make
make run
# or
g++ -std=c++17 -o sql_parser sql_parser.cpp && ./sql_parser
```
> The lab text relied on `std::pow`; this submission adds `#include <cmath>` so it
> compiles cleanly with `-Wall -Wextra`.

## Part 1 — Shunting-Yard
SQL WHERE clauses are infix expressions like `age > 25 AND salary * 1.1 < 90000`.
Shunting-Yard converts infix → postfix (RPN) in O(n) with a stack; the RPN is then
evaluated with a second stack. Operator **precedence** and **associativity** are the
two properties that determine output order.

## Part 2 — Minimal SQL executor
A minimal executor is just four steps:
```
filter (WHERE)  ->  project (SELECT columns)  ->  sort (ORDER BY)  ->  truncate (LIMIT)
```
String columns are handled with `std::variant<double, std::string>`, so values are
typed rather than raw doubles.

## How the pieces map to a real database
```
SQL string
  -> Lexer / Tokenizer   (tokenize)
  -> Parser              (parse_select  -> SelectQuery AST)
  -> Planner             (not implemented; would pick index vs full scan)
  -> Executor            (execute: WHERE via Shunting-Yard, then project/sort/limit)
  -> Result set          (vector<Row>)
```
In a real DB the WHERE expression is compiled into an expression tree by the
planner once, not re-parsed per row — Shunting-Yard is the mechanism that builds
that tree. The `vector<Row>` here simulates the output of a storage layer that
already fetched pages from disk (as in Labs 2–3).

## Key Takeaways
- Shunting-Yard converts infix to RPN in O(n) with a stack — no recursion needed.
- Precedence + associativity fully determine the conversion.
- A minimal SQL executor is filter → project → sort → truncate.
- `std::variant` cleanly supports typed (string/number) column values in C++17.
