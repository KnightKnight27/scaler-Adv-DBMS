# Lab 5 — Shunting-Yard Algorithm + Minimal SQL SELECT Parser

**Rohan Ranjan — 24BCS10428**

## Objective
1. Implement Dijkstra's Shunting-Yard algorithm to evaluate infix arithmetic/boolean
   expressions (the mechanism behind SQL `WHERE`-clause evaluation).
2. Build a minimal SQL parser that handles `SELECT` queries and runs them against an
   already-fetched `vector<Row>` in memory.

## Build & run
```bash
g++ -std=c++17 -o sql_parser sql_parser.cpp
./sql_parser
```
(All parts live in the single file `sql_parser.cpp`. `#include <cmath>` is added for
`std::pow`, which the lab text used in `eval_rpn` without including.)

## Part 1 — Shunting-Yard
A SQL `WHERE` clause is an infix expression like `age > 25 AND salary * 1.1 < 90000`.
Shunting-Yard converts infix → postfix (RPN) in O(n) using an operator stack; the RPN is
then evaluated with a single value stack. Operator **precedence** and **associativity**
are the two properties that determine the output order.

Example:
```
Expression : age * 2 + salary / 1000 > 100
RPN        : age 2 * salary 1000 / + 100 >
Result     : true        (age=30, salary=50000)
```

## Part 2 — Minimal SQL SELECT over `vector<Row>`
- `Row` stores columns as `std::variant<double, std::string>` so string and numeric
  columns coexist cleanly.
- `parse_select` produces a `SelectQuery` (columns, from, raw WHERE, ORDER BY, LIMIT).
- `execute` runs the pipeline: **filter (WHERE)** → **project (columns)** →
  **sort (ORDER BY)** → **truncate (LIMIT)**. The WHERE clause is evaluated per row by
  feeding the row's columns as variables into the Shunting-Yard RPN evaluator.

Sample queries:
```sql
SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3
SELECT * FROM students WHERE age >= 22 && age <= 26
```

## How the pieces map to a real database
```
SQL string -> Lexer/Tokenizer (tokenize)
           -> Parser (parse_select -> SelectQuery AST)
           -> Planner (not implemented; would pick index vs full scan)
           -> Executor (execute: WHERE via Shunting-Yard, project, sort, limit)
           -> Result set (vector<Row>)
```
In a real DB the WHERE expression is compiled into an expression tree once by the planner
(not re-parsed per row); Shunting-Yard is the mechanism that builds that tree. The
`vector<Row>` here stands in for the output of the storage layer from Labs 2–3.

## Key takeaways
- Shunting-Yard converts infix to RPN in O(n) with a stack — no recursion needed.
- Precedence + associativity fully determine the RPN ordering.
- A minimal SQL executor is just: filter → project → sort → truncate.
- `std::variant` cleanly supports typed (string/number) columns in C++17.
