# Lab 5 — Shunting-Yard + Minimal SQL SELECT Parser

## Files
- `sql_parser.cpp` — Part 1 (Shunting-Yard + RPN eval) + Part 2 (SQL parser)

## Build & Run
```bash
g++ -std=c++17 -o sql_parser sql_parser.cpp && ./sql_parser
```

## Part 1: Shunting-Yard
Converts infix expressions to **postfix (RPN)** in O(n) with a stack.
The RPN evaluator uses a second stack to compute the result.

**Operator precedence (higher = tighter binding):**
```
||  OR           1
&&  AND          2
=  !=            3
< > <= >=        4
+ -              5
* /              6
^  power         7 (right-associative)
```

**Worked example:**
```
Infix:  age * 2 + salary / 1000 > 100
RPN:    age 2 * salary 1000 / + 100 >
```
Each RPN token is pushed onto a stack; on operator, pop two values,
apply op, push result.

## Part 2: SQL parser
Supports:
- `SELECT col1, col2` or `SELECT *`
- `FROM table` (table name is symbolic — data is pre-fetched)
- `WHERE expr` (uses Shunting-Yard evaluator from Part 1)
- `ORDER BY col [ASC|DESC]`
- `LIMIT n`

Pipeline:
```
SQL string → parse_select() → SelectQuery (AST)
          → execute(query, vector<Row>) → vector<Row>
              ├── WHERE filter  (Shunting-Yard eval per row)
              ├── projection    (select columns)
              ├── ORDER BY      (std::sort)
              └── LIMIT         (truncate)
```

## How this connects to a real DB
```
SQL string
   |
Lexer / Tokenizer   ← tokenize()
   |
Parser              ← parse_select()  produces SelectQuery AST
   |
Planner             ← (not implemented) would choose index vs full scan
   |
Executor            ← execute()  iterates vector<Row>, evaluates WHERE
   |
Result set          ← vector<Row>
```
- In a real DB, the WHERE expression is compiled into an expression tree once
  by the planner, not re-parsed per row. Shunting-Yard builds that tree.
- The `vector<Row>` simulates the output of a storage layer that already
  fetched pages from disk (Labs 2–3).

## Key takeaways
- Shunting-Yard: infix → RPN in O(n) with a stack — no recursion.
- Operator precedence + associativity are the two properties that determine
  the RPN output order.
- A minimal SQL executor is just: **filter → project → sort → truncate**.
- String columns require typed variants, not raw doubles — `std::variant`
  makes this clean in C++17.