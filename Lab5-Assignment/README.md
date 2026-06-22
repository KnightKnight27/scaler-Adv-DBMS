# Lab Session 5 — Solution: Shunting-Yard + Minimal SQL SELECT Engine (C++)

My completed solution to **`lab_sessions/lab_5.txt`** — Dijkstra's Shunting-Yard
expression evaluator plus a minimal SQL `SELECT` engine that runs `WHERE` /
projection / `ORDER BY` / `LIMIT` over an in-memory `vector<Row>`. Compiled and
run, with captured output.

## Files

| File | Purpose |
|------|---------|
| `sql_parser.cpp` | Tokenizer + Shunting-Yard + RPN evaluator + SQL parser + executor |
| `run_output.txt` | Captured output of `./sql_parser` |
| `.gitignore` | Excludes the compiled binary |

```bash
g++ -std=c++17 -O2 -o sql_parser sql_parser.cpp
./sql_parser
```

---

## Build fixes vs the handout

The handout split the program across snippets whose include list omitted three
headers actually used by the code, so it would not compile as written:

| Used | Needs header |
|------|--------------|
| `std::pow` (the `^` operator) | `<cmath>` |
| `std::variant`, `std::get_if` (typed `Value`) | `<variant>` |
| `std::function` (used in the lab-6 style helpers) | `<functional>` |

I added them and merged the snippets into one translation unit. I also made the
result printing deterministic (print in the `SELECT` column order) so the output
is stable across runs.

---

## Part 1 — Shunting-Yard: infix → RPN → value

A SQL `WHERE` clause like `age * 2 + salary / 1000 > 100` is an **infix**
expression. Shunting-yard converts it to **postfix (RPN)** in one linear pass
using an operator stack driven by two properties — **precedence** and
**associativity** — after which a tiny stack machine evaluates it.

**Real output:**
```
Expression : age * 2 + salary / 1000 > 100
RPN        : age 2 * salary 1000 / + 100 >
With age=30, salary=50000 -> true
```

The RPN ordering proves precedence was honoured: `*` and `/` (precedence 6) bind
tighter than `+` (5), which binds tighter than `>` (4), so both multiplicative
sub-terms are emitted before the `+`, and the comparison is emitted last.
Evaluating with `age=30, salary=50000`: `30*2 + 50000/1000 = 60 + 50 = 110`, and
`110 > 100` → **true**. ✔

Why RPN? Once in postfix there are **no parentheses and no precedence left to
reconsider** — evaluation is a trivial push-operand / pop-two-apply-operator
loop. That is the whole point of the transform.

---

## Part 2 — Minimal SQL `SELECT` over `vector<Row>`

The `vector<Row>` stands in for rows a storage layer already fetched from disk
(as in Labs 2–3). A row is a `variant<double,string>` map, so columns can be
numeric or text. The executor is the canonical relational pipeline:

```
WHERE (filter)  →  SELECT cols (project)  →  ORDER BY (sort)  →  LIMIT (truncate)
```

`WHERE` reuses Part 1: the clause is tokenised + converted to RPN **once**, then
evaluated per row against a variable map built from that row's columns.

**Real output:**
```
SQL: SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3
  id=1  name=Alice  gpa=3.8
  id=3  name=Carol  gpa=3.5
  id=4  name=Dave   gpa=3.1
  (3 rows)

SQL: SELECT * FROM students WHERE age >= 22 && age <= 26
  ... name=Alice age=22 id=1 ...
  ... name=Bob   age=25 id=2 ...
  (2 rows)

SQL: SELECT name, age FROM students WHERE age > 21 ORDER BY age ASC
  name=Alice age=22
  name=Bob   age=25
  name=Dave  age=30
  (3 rows)
```

Verifying each query against the 4-row table (Alice 22/3.8, Bob 25/2.9, Carol
21/3.5, Dave 30/3.1):

- **Q1** `gpa > 3.0`: Bob (2.9) is filtered out; the rest sorted by gpa **DESC**
  → 3.8, 3.5, 3.1; `LIMIT 3` keeps all three. ✔
- **Q2** `age >= 22 && age <= 26`: only Alice (22) and Bob (25). The `&&` is
  evaluated by the *same* shunting-yard engine — `WHERE` is just another infix
  expression. ✔ (For `SELECT *` the column print order is arbitrary, as noted in
  the code.)
- **Q3** `age > 21` ordered **ASC**: Carol (21) excluded; 22 < 25 < 30. ✔

---

## How this maps onto a real database

```
SQL string
   │  tokenize()                  ← lexer
   ▼
tokens
   │  parse_select()              ← parser → SelectQuery ("AST")
   ▼
SelectQuery
   │  (planner — not built here)  ← would choose index scan vs full scan
   ▼
execute()                          ← executor: filter → project → sort → limit
   ▼
vector<Row>  (result set)
```

Two honest differences from a production engine, worth calling out:

1. **A real planner compiles `WHERE` into an expression tree once** and may
   re-order predicates or use an index to avoid scanning every row. Here every
   row is scanned and the RPN is evaluated per row — correct, but the "full table
   scan" plan only.
2. **Everything is coerced to `double` for evaluation.** Real SQL is typed;
   string comparison, NULLs, and type affinity (seen in Lab 2's SQLite `SERIAL`)
   need a typed evaluator. `std::variant` is the clean C++17 way to carry the
   types, which this code uses for storage even though arithmetic is numeric.

---

## Design trade-offs

- **Parse the predicate once, evaluate many times.** Converting `WHERE` to RPN
  up-front and reusing it per row is the small-scale version of why DBs compile,
  not re-parse, expressions in the hot loop.
- **Operator-precedence parsing (shunting-yard) vs a recursive-descent parser:**
  shunting-yard is `O(n)`, iterative (no recursion/stack-overflow risk), and
  trivial to extend — just add rows to the `OPS` table. The cost is that it only
  produces a flat RPN stream, not a rich AST, which a real planner would want.
- **`vector<Row>` linear scan** is `O(rows × predicate)` — fine for a buffer of
  already-fetched rows, but the reason real systems add indexes is to avoid
  touching every row in the first place.

---

## Key learnings

- Shunting-yard turns infix into RPN in `O(n)` with a single operator stack;
  **precedence and associativity are the only two inputs** that decide output
  order — visible directly in the emitted RPN.
- A minimal SQL executor is genuinely just **filter → project → sort → truncate**;
  the `WHERE` evaluator and the expression evaluator are the *same* component.
- Typed columns need `std::variant`, not raw doubles — the same typing question
  that SQLite's dynamic typing raised in Lab 2.
- The lexer → parser → planner → executor split here is a faithful miniature of
  how PostgreSQL processes a query string.

---

### Reference

- Solution to `lab_sessions/lab_5.txt` (Advanced DBMS lab series).
- Dijkstra's shunting-yard algorithm (operator-precedence parsing).
- Built with `g++` 13.3.0 `-std=c++17`, Ubuntu 24.04.
