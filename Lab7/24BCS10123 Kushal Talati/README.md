# Lab 7 — Shunting-Yard + a Tiny SQL SELECT Engine

**Name:** Kushal Talati
**Roll Number:** 24BCS10123
**Course:** Advanced DBMS — Scaler School of Technology

A header-only query engine (`kt::QueryEngine`) that runs
`SELECT … FROM … WHERE … ORDER BY … LIMIT …` against an in-memory
`kt::Relation`. It is the front end of a database in miniature: a hand-written
lexer feeds a recursive-descent compiler, the `WHERE` clause is rewritten from
infix to postfix with Dijkstra's **shunting-yard**, and the executor applies the
clauses in SQL's logical order.

The one idea worth carrying out of this lab: the `WHERE` predicate is compiled to
postfix **exactly once**, when the statement is compiled — *not* re-parsed for
every row. The per-row hot loop is then a single linear stack pass over the RPN.
That is precisely how a real planner compiles a filter once and applies it to
every tuple a scan node produces.

It reuses the layout my earlier labs settled on: one `.hpp` (the whole engine),
a `runner.cpp` driver, a `CMakeLists.txt`, and this README — the same shape as
the Lab-6 B-tree index.

---

## What lives in this folder

```
Lab7/24BCS10123 Kushal Talati/
├── mini_sql.hpp     # kt::QueryEngine — Cell/Tuple/Relation, lexer, shunting-yard, compiler, executor, printer
├── runner.cpp       # five-section demo, each assertion self-checked
├── CMakeLists.txt   # C++17 build with -Wall -Wextra -Wpedantic -Wshadow
└── README.md        # this file
```

---

## Build and run

```bash
# CMake
cmake -S . -B build && cmake --build build
./build/mini_sql_lab

# One-liner
clang++ -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -O2 \
    runner.cpp -o mini_sql_lab && ./mini_sql_lab
```

Tested with Apple clang on macOS arm64. Builds with zero warnings; the run ends
with `All mini-SQL engine checks passed.`

---

## The pipeline

```
SQL text
   │  lex()         hand-written lexer: keywords, names, int/text literals, operators
   ▼
tokens
   │  compile()     recursive descent; the WHERE slice goes through to_postfix()
   ▼
Query { select, from, filter (postfix), sort_key, sort_desc, cap }
   │  run()         filter (matches() per row) → project → sort → cap
   ▼
result Relation  ──► render()
```

Every stage is a public method, so the driver can show each one in isolation —
section (a) prints the postfix form on its own, and section (e) compiles a bare
predicate and walks it row by row without ever building a `Query`.

---

## Why postfix at all?

An infix expression cannot be read left-to-right without remembering precedence
and tracking parentheses. Postfix (RPN) needs neither: the position of an
operator already says exactly when it runs, so evaluation is one pass with one
stack. The shunting-yard is the standard infix→postfix conversion:

- an **operand** (column / number / text) goes straight to the output,
- an **operator `o`** first pops every stacked operator that binds at least as
  tightly as `o`, then pushes `o`,
- `(` is pushed; `)` pops back to the matching `(`.

Operator precedence used here (higher binds tighter):

| Power | Operators | Associativity |
|-------|-----------|---------------|
| 4 | `=` `!=` `<` `<=` `>` `>=` | left |
| 3 | `NOT` (unary)             | right |
| 2 | `AND`                     | left |
| 1 | `OR`                      | left |

Worked example from the demo:

```
infix   : age > 25 AND (dept = 'Sales' OR salary >= 100000)
postfix : age 25 > dept 'Sales' = salary 100000 >= OR AND
```

`matches()` then walks that postfix list with one stack: an operand pushes its
value (a name resolves to the row's column), a binary operator pops two and
pushes a `0/1` result, unary `NOT` pops one. `AND`/`OR`/`NOT` read their operands
by truthiness.

---

## Typed cells

Each cell is a `std::variant<long long, std::string>`. Comparisons are
type-aware: two integers compare numerically, two strings lexically, and a
number-vs-text mismatch is reported as **unorderable** (the comparison yields
false) rather than silently coerced. That is what lets `dept = 'Sales'` and
`salary >= 100000` coexist inside one predicate. Text literals are
single-quoted, and a doubled quote `''` is one literal quote inside the string —
handled in the lexer.

---

## The SELECT grammar handled

```
SELECT  ( '*' | name (',' name)* )
FROM    table
[ WHERE  <boolean expression> ]
[ ORDER BY name ( ASC | DESC ) ]
[ LIMIT  n ]
```

`run()` applies the clauses in SQL's logical evaluation order:

1. **WHERE** — keep the rows where `matches()` is true,
2. **projection** — copy only the selected columns (`*` keeps the whole heading),
3. **ORDER BY** — `std::stable_sort` on the chosen output column (ASC/DESC),
4. **LIMIT** — truncate the result set.

---

## What `./mini_sql_lab` exercises

| Section | What it runs | Asserted |
|---------|--------------|----------|
| (a) | shunting-yard trace on three clauses (AND/OR/parens, a `NOT`, a chain of `OR`s) | the first clause's RPN matches the expected token order |
| (b) | `SELECT * FROM employees` | all 8 rows, all 5 columns |
| (c) | projection + `WHERE age > 25 AND (dept = 'Sales' OR salary >= 100000)` + `ORDER BY salary DESC` | 3 rows, Anaya first, Kabir last |
| (d) | `NOT`, a text comparison, `ORDER BY age ASC`, `LIMIT 2` | 2 rows, Vivaan first, second is age 24 |
| (e) | compile a predicate once, evaluate it per row via `matches()` | Rohan and Anaya match |

Each line prints `[ok]`/`[XX]`; the program exits non-zero on the first failure.

---

## Complexity

| Stage | Cost |
|-------|------|
| lex          | `O(L)` in the SQL length |
| shunting-yard | `O(t)` in the number of WHERE tokens |
| `matches` per row | `O(t)` — one stack pass over the postfix |
| `run`         | `O(n · t)` filter + `O(n)` project + `O(n log n)` sort |

The DBMS-relevant point: the `O(t)` compile happens once; only the cheap `O(t)`
stack walk runs inside the per-row loop.

---

## How this differs from a straight textbook build

| Typical write-up | This implementation |
|------------------|---------------------|
| `tokenize` returns a `Tok` enum; `eval_rpn`/`parse_select` are free functions split across a `.h` + `.cc` | One header-only `kt::QueryEngine` class. Stages are member functions so the driver can call `lex` / `to_postfix` / `matches` independently. |
| `std::stack<…>` for the operator stack | A `std::vector<Token>` used as the stack — same asymptotics, but it avoids dragging in `<stack>` and keeps the operator list contiguous. |
| Column/row/table named `Value` / `Row` / `Table` | `Cell` / `Tuple` / `Relation` — relational-algebra names, and they don't collide with the `Value`/`Row` types my Lab-8 transaction manager uses. |
| `print_table` lives next to the parser | `render` is a `static` member; it needs no engine state, which the signature now makes explicit. |

The shunting-yard and the per-row RPN evaluation are the textbook algorithms —
there is no alternative worth implementing. The originality is in the API surface
(one engine class, individually traceable stages), the type naming, and the
self-checking driver.

---

## Connections to other labs

| Lab | Connection |
|-----|------------|
| Lab 6 (B-tree ordered index) | `run()` does a full table scan here. The moment `employees` grows, the `WHERE id = …` path would descend the Lab-6 B-tree instead of scanning — the predicate compilation in this lab is exactly the filter a real index-scan node still evaluates on the rows it returns. |
| Lab 5 (red-black tree) | Same point as Lab 6 for an in-memory index: `ORDER BY` is a sort here, but an ordered index would let the planner skip it and walk the tree in key order. |
| Lab 4 (SQLite hex-dump) | That lab read a stored table off disk; this lab is the query side that would consume those rows once decoded into `Tuple`s. |

---

## Reproducing the run

```bash
cmake -S . -B build && cmake --build build && ./build/mini_sql_lab
# Expected last line:  All mini-SQL engine checks passed.
```
