# Lab 7 — Shunting-Yard + Minimal SQL SELECT Engine (C++17)

**Name:** Gauri Shukla
**Roll Number:** 24BCS10115
**Course:** Advanced DBMS — Scaler School of Technology

A two-part lab that builds the front end of a tiny SQL engine:

1. **Shunting-Yard** — Dijkstra's algorithm rewrites an infix `WHERE` clause into
   postfix (RPN), so operator precedence and parentheses are baked into the token
   order and the predicate can be evaluated in one left-to-right stack pass.
2. **SELECT engine** — a tokenizer → parser → executor pipeline that runs
   `SELECT … FROM … WHERE … ORDER BY … LIMIT …` against an in-memory table of
   typed rows.

The `WHERE` clause is compiled to RPN **once** by `parse_select`, then evaluated
per row by `eval_rpn` — exactly how a real planner compiles a filter once and
applies it to every tuple in the scan.

---

## Files

```
Lab-7/
├── sql_engine.h    # data model (Table/Row/Value), tokenizer, shunting-yard, SELECT API
├── sql_engine.cc   # tokenizer, shunting_yard, eval_rpn, parse_select, execute, printer
├── main.cc         # Part-1 RPN trace + Part-2 SELECT demo, with assertions
└── Makefile        # c++17 build, -Wall -Wextra -Wpedantic -Wshadow
```

## Build & run

```bash
make run
# or manually:
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic main.cc sql_engine.cc -o sql_demo && ./sql_demo
```

Tested with Apple clang 21 on macOS arm64 — zero warnings, exits `0`, prints
`All SQL engine checks passed.`

---

## 1. The pipeline

```
SQL text
   │  tokenize()         lexer: keywords, identifiers, int/string literals, operators
   ▼
tokens
   │  parse_select()     recursive-descent over the token stream;
   │                     the WHERE slice is handed to shunting_yard()
   ▼
SelectStmt { projection, table, where_rpn, order_by, limit }
   │  execute()          filter (eval_rpn per row) → project → order → limit
   ▼
result Table  ──► print_table()
```

## 2. Shunting-Yard (Part 1)

Infix expressions need parentheses and precedence rules to be read correctly.
Postfix needs neither — the position of each operator already says when it runs.
The conversion uses an output queue and an operator stack:

- **operand** (column / number / string) → push to the output queue,
- **operator `o1`** → first pop every stacked operator that binds at least as
  tightly, then push `o1`,
- **`(`** → push; **`)`** → pop until the matching `(`.

Operator precedence used here (higher binds tighter):

| Rank | Operators | Associativity |
|------|-----------|---------------|
| 4 | `=` `!=` `<` `<=` `>` `>=` | left |
| 3 | `NOT` (unary) | right |
| 2 | `AND` | left |
| 1 | `OR` | left |

Example from the demo:

```
infix : age > 25 AND (dept = 'Sales' OR salary >= 100000)
RPN   : age 25 > dept 'Sales' = salary 100000 >= OR AND
```

`eval_rpn` then walks the RPN with one stack: operands push their value (an
identifier resolves to the row's column), a binary operator pops two and pushes
the result, and unary `NOT` pops one. Comparisons yield `0/1`; `AND`/`OR`/`NOT`
read operands by truthiness.

## 3. Typed values

Every cell is a `std::variant<long long, std::string>`. Comparisons are
type-aware: two integers compare numerically, two strings lexicographically, and
a number-vs-text mismatch is treated as “unorderable” (the comparison is false)
rather than silently coerced. This is what lets `dept = 'Sales'` and
`salary >= 100000` live in the same predicate.

## 4. The SELECT grammar handled

```
SELECT  ( '*' | col (',' col)* )
FROM    table
[ WHERE  <boolean expression> ]
[ ORDER BY col ( ASC | DESC ) ]
[ LIMIT  n ]
```

`execute()` applies the stages in SQL's logical order:

1. **WHERE** — keep rows where `eval_rpn` is true,
2. **projection** — copy only the selected columns (`*` keeps all),
3. **ORDER BY** — `stable_sort` on the chosen column (ASC/DESC),
4. **LIMIT** — truncate the result.

## 5. What `./sql_demo` runs

- **Part 1** — prints the RPN for three infix `WHERE` clauses (AND/OR/parens, a
  `NOT`, and a chain of `OR`s).
- **Part 2a** — `SELECT * FROM employees` (all 8 rows, aligned table output).
- **Part 2b** — projection + `WHERE age > 25 AND (dept = 'Sales' OR salary >= 100000)`
  + `ORDER BY salary DESC`; asserts the 3 expected rows in the right order.
- **Part 2c** — `NOT`, a string comparison, `ORDER BY age ASC`, and `LIMIT 2`.
- **Part 2d** — compiles a predicate once and evaluates it row-by-row via
  `eval_rpn`, the way a scan node applies a filter.

Each scenario checks its result with assertions; any mismatch aborts the run.

## 6. Complexity

| Stage | Cost |
|-------|------|
| tokenize | `O(L)` in the SQL length |
| shunting-yard | `O(t)` in the number of WHERE tokens |
| eval per row | `O(t)` — one stack pass over the RPN |
| execute | `O(n · t)` filter + `O(n)` project + `O(n log n)` sort |

The point worth keeping from a DBMS angle: the WHERE clause is parsed and
converted to RPN **once**, not re-parsed for every row — only the cheap `O(t)`
stack evaluation runs in the per-row hot loop.

## 7. Design notes

- Modular `.h`/`.cc` split with a `Makefile`, consistent with my Lab-5/Lab-6
  submissions.
- Goes beyond an int-only evaluator: **typed (int + string) columns**, the `!=`
  and unary `NOT` operators, and `ORDER BY` / `LIMIT` on top of the core
  shunting-yard filter.
- Single-quoted string literals with `''` escaping, handled in the tokenizer.
