<!-- ============================================================ -->
<!-- SUBMISSION CHECKPOINT                                        -->
<!--   Name     : Abdul Kalam Azad                               -->
<!--   Roll No. : 24BCS10053                                     -->
<!--   Lab      : Shunting-Yard + Minimal SQL SELECT Parser      -->
<!-- ============================================================ -->

# Shunting-Yard Algorithm + Minimal SQL SELECT Parser over vector&lt;Row&gt;

**Author:** Abdul Kalam Azad
**Roll No.:** 24BCS10053

## Objective
1. Implement Dijkstra's **Shunting-Yard** algorithm to evaluate infix
   arithmetic/boolean expressions (as used in SQL `WHERE` clauses).
2. Build a **minimal SQL SELECT parser** that executes queries (`WHERE`,
   `ORDER BY`, `LIMIT`) against an already-fetched `vector<Row>` in memory.

## Files

```text
sql_parser.cpp   Part 1 (Shunting-Yard) + Part 2 (SQL SELECT parser) + demo
README.md        this file
```

## Build & Run

```bash
g++ -std=c++17 -o sql_parser sql_parser.cpp && ./sql_parser
```

> Note: the assignment brief omitted `#include <cmath>`, which `std::pow`
> (the `^` operator) needs — it is included here so the file compiles cleanly.

## Part 1 — Shunting-Yard (expression evaluator)

Pipeline: `tokenize()` -> `to_rpn()` (infix to postfix) -> `eval_rpn()`.
Operator precedence and associativity are encoded in the `OPS` table, so a
single stack pass converts infix to Reverse Polish Notation in O(n).

## Part 2 — Minimal SQL SELECT parser

- `Row` holds columns as `std::variant<double, std::string>` so both numeric
  and text values are supported (C++17).
- `parse_select()` produces a `SelectQuery` (columns, from, where_raw,
  order_by, order_asc, limit).
- `execute()` is the tiny query engine:
  **filter (WHERE)** -> **project (columns)** -> **sort (ORDER BY)** -> **truncate (LIMIT)**.
  The WHERE clause is evaluated per row by reusing the Shunting-Yard evaluator.

## Expected output

```text
Expression : age * 2 + salary / 1000 > 100
RPN        : age 2 * salary 1000 / + 100 >
Result     : true

SQL: SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3
id=1  name=Alice  gpa=3.8
id=3  name=Carol  gpa=3.5
id=4  name=Dave  gpa=3.1

SQL: SELECT * FROM students WHERE age >= 22 && age <= 26
id=1  name=Alice  age=22  gpa=3.8
id=2  name=Bob  age=25  gpa=2.9
```

The query results were verified by porting the same logic and checking the
filter/order/limit output against a reference implementation.

> The order of **columns within a single row** is not fixed, because `Row`
> uses `std::unordered_map`. The **row order** (from `ORDER BY`) and the
> **filtering** are what the query controls, and those are deterministic.

## How the pieces connect to a real database

```text
SQL string
   |
Tokenizer   <- tokenize()
   |
Parser      <- parse_select()  produces a SelectQuery
   |
Executor    <- execute()  scans vector<Row>, evaluates WHERE via Shunting-Yard
   |
Result set  <- vector<Row>
```

- The `vector<Row>` simulates rows already fetched from the storage layer.
- `ORDER BY` maps to a sort node in a real query plan; `WHERE` maps to a filter.

## Key Takeaways
- Shunting-Yard converts infix to RPN in O(n) using a single operator stack.
- Precedence + associativity fully determine the output order of operators.
- A minimal SQL executor is just: filter -> project -> sort -> truncate.
- `std::variant` cleanly models typed (numeric **or** string) column values.

---

**Submitted by:** Abdul Kalam Azad &nbsp;|&nbsp; **Roll No.:** 24BCS10053
