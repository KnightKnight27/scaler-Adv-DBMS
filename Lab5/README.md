# Lab 5 — Shunting-Yard + Minimal SQL SELECT Parser (C++)

Two pieces that together form the query-evaluation core of a tiny DB:

1. **Dijkstra's Shunting-Yard algorithm** — converts an infix expression
   (the kind used in SQL `WHERE` clauses) into reverse-Polish notation,
   then evaluates it with a stack.
2. **A minimal SQL `SELECT` parser + executor** — parses
   `SELECT … FROM … WHERE … ORDER BY … LIMIT …`, then runs it against a
   pre-loaded `std::vector<Row>`.

Follows `lab_sessions/lab_5.txt` from sir's repo.

## Build & run

```bash
cd Lab5
cmake -S . -B build
cmake --build build
./build/sql_parser
```

Requires CMake ≥ 3.10 and a C++17 compiler.

## Part 1 — Shunting-Yard

Operator table (precedence + associativity):

| Op  | Prec | Assoc | Meaning |
|-----|------|-------|---------|
| `\|\|` | 1 | left | logical OR |
| `&&` | 2 | left | logical AND |
| `=`, `!=` | 3 | left | equality |
| `<`, `>`, `<=`, `>=` | 4 | left | comparison |
| `+`, `-` | 5 | left | additive |
| `*`, `/` | 6 | left | multiplicative |
| `^`     | 7 | right | exponentiation |

Pipeline: `tokenize()` → `to_rpn()` → `eval_rpn(rpn, vars)`. The variable
map lets you bind `age`, `salary`, etc. to row column values.

## Part 2 — SQL SELECT

`SelectQuery` is the parsed AST:

```cpp
struct SelectQuery {
    std::vector<std::string> columns;   // empty == SELECT *
    std::string              from;
    std::string              where_raw; // raw expression string
    std::string              order_by;
    bool                     order_asc = true;
    int                      limit = -1;
};
```

`execute(query, data)` does:

1. **Filter** — convert `where_raw` to RPN once, evaluate per row.
2. **Project** — keep only the requested columns (or all if `SELECT *`).
3. **Sort** — `std::sort` on the `ORDER BY` column.
4. **Truncate** — apply `LIMIT`.

This is the same shape as a real query engine's executor, just running
on a `vector<Row>` instead of pages from disk.

## What `main()` demonstrates

- **Shunting-yard demo** evaluating `age * 2 + salary / 1000 > 100`
  against `{age: 30, salary: 50000}` → `true`.
- Three `SELECT` queries over a 4-row `students` dataset:
  - `WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3` — filter + sort + limit
  - `WHERE age >= 22 && age <= 26` — compound boolean WHERE via shunting-yard
  - `WHERE gpa > 3.4 ORDER BY age` — projection of just two columns

## How this fits into a real DB

```
SQL string
   │
Lexer        <- tokenize()
   │
Parser       <- parse_select()     produces SelectQuery AST
   │
Planner      <- (not built here)   would pick index vs full scan
   │
Executor     <- execute()          filter -> project -> sort -> limit
   │
Result set   <- vector<Row>
```

Lab 2 gave us the data (pre-fetched from SQLite/Postgres). Lab 3 gave
us the buffer pool that would feed those pages. Lab 5 is the query
layer that runs on top.

## File layout

| File             | Purpose |
|------------------|---------|
| `main.cpp`       | Shunting-yard + SELECT parser + executor + demo |
| `CMakeLists.txt` | C++17 build |
| `.gitignore`     | excludes `build/` and the binary |
