### Daksh Shah - 24bcs10092 
# Lab 6 — Shunting-Yard Algorithm + Minimal SQL SELECT Parser

## Parts

**Part 1 – Shunting-Yard Algorithm**  
Converts infix arithmetic/boolean expressions to Reverse Polish Notation (RPN) using Dijkstra's algorithm, then evaluates the RPN with a stack. Supports `+`, `-`, `*`, `/`, `^`, `<`, `>`, `<=`, `>=`, `=`, `!=`, `&&`, `||`.

**Part 2 – Minimal SQL SELECT Parser**  
Parses and executes `SELECT` queries against an in-memory `vector<Row>`. Supports `SELECT *` / column projection, `WHERE` (evaluated via Part 1's shunting-yard evaluator), `ORDER BY [ASC|DESC]`, and `LIMIT`.

Completed both the task in a single code file `sql_parser.cpp`

## Build & Run

```bash
g++ -std=c++14 -o sql_parser sql_parser.cpp
./sql_parser
```