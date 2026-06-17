# Lab 7 — Shunting-Yard + Mini SQL SELECT Engine (C++17)

**Name:** Vibhuti Bhatnagar
**Role Number:** 24BCS10288
**Course:** Advanced DBMS — Scaler School of Technology

A two-part lab building the front end of a tiny SQL engine in C++17:

1. **Shunting-Yard** — Dijkstra's classic algorithm rewriting an infix `WHERE` expression into postfix (RPN). Precedence and parentheses end up encoded in token order, so the predicate evaluates in a single linear pass over an evaluation stack.
2. **SELECT engine** — a `lex → parse → run` pipeline executing
   ```sql
   SELECT (col_list | * | COUNT(*))
   FROM   table
   WHERE  <predicate>
   ORDER BY col [ASC|DESC]
   LIMIT  n
   ```
   over an in-memory typed table.

The whole engine fits in one header file (`sql_engine.hpp`). The demo driver builds a books catalogue, asks several questions, and verifies each answer with an assertion.

---

## Files

```
Lab7/24BCS10288 Vibhuti Bhatnagar/
├── sql_engine.hpp    # header-only namespace adbms::sql — lex, RPN, parse, run, print
├── main.cpp          # 7-scenario demo with hard assertions
├── CMakeLists.txt    # C++17 build with -Wall -Wextra -Wpedantic -Wshadow
└── README.md         # this file
```

## Build & run

```bash
# CMake
cmake -S . -B build && cmake --build build && ./build/sql_engine_demo

# one-liner
clang++ -std=c++17 -Wall -Wextra -Wpedantic -O2 main.cpp -o sql_engine_demo && ./sql_engine_demo
```

Tested on macOS arm64 with Apple clang 21.0 — zero warnings, exit code 0, final line `All SQL engine checks passed.`

---

## 1. The pipeline

```
SQL text
   │
   │  (1) lex
   ▼
[ Token, Token, … ]                 every literal / identifier / operator
   │                                with its kind and source text
   │  (2) parse
   ▼
Query {                             outer skeleton is recursive-descent;
   projection / is_count_star          the WHERE expression is sliced out
   table                                and converted to RPN once, then
   where_rpn   (postfix)               stored as a flat token vector.
   order_col, order_desc
   limit
}
   │  (3) run
   ▼
Relation (result set)               filter → order → limit → project
```

The interesting moment is step 2 — splitting the parser into "easy outer skeleton" and "hard inner expression" lets us delegate the precedence problem to a small, well-understood algorithm.

---

## 2. The shunting-yard algorithm — Dijkstra (1961)

Convert infix expressions to postfix using two structures:

* an **output queue** that accumulates the RPN
* an **operator stack** that holds operators waiting for their right operand

Per token:

| Token kind        | Action |
|-------------------|--------|
| Identifier / int / string literal | Push to output. |
| Operator `o₁`     | While the operator on top of the stack has higher precedence, **or** equal precedence and `o₁` is left-associative, pop it to output. Push `o₁`. |
| `(`               | Push to stack. |
| `)`               | Pop until `(`; discard the `(`. |
| End of input      | Pop everything remaining to output. |

The result holds operators in **evaluation order** — no parentheses needed, no precedence lookups at evaluation time. Cost: O(n) tokens in, O(n) tokens out.

### Operator precedence used here

| Operator                          | Precedence | Associativity |
|-----------------------------------|-----------:|---------------|
| `=` `!=` `<` `<=` `>` `>=` `LIKE` | 4          | left          |
| `NOT`                             | 3          | right (unary) |
| `AND`                             | 2          | left          |
| `OR`                              | 1          | left          |

This matches the SQL-standard reading of `NOT a = 1 OR b = 2` as `((NOT (a = 1)) OR (b = 2))`.

### Worked example

Infix:
```
year >= 2000 AND (genre = 'Tech' OR pages > 400)
```

After shunting-yard, the demo prints:
```
year 2000 >= genre 'Tech' = pages 400 > OR AND
```

Reading the RPN left-to-right on an evaluation stack:

1. push `year`, push `2000`, see `>=` → pop two, compare, push the boolean
2. push `genre`, push `'Tech'`, see `=` → pop two, compare, push the boolean
3. push `pages`, push `400`, see `>` → pop two, compare, push the boolean
4. see `OR`  → pop two booleans, push their OR
5. see `AND` → pop two booleans, push their AND
6. one value left on the stack → that's the row's WHERE result.

That's exactly what `run_rpn()` does in `sql_engine.hpp`.

---

## 3. Tokens and types

Every cell is `std::variant<std::int64_t, std::string>`, so a column can be an integer or a string. The lexer recognises:

* **Identifiers** — `[A-Za-z_][A-Za-z0-9_]*`
* **Integer literals** — optional leading `-` followed by digits
* **String literals** — `'...'` with `''` doubling for an embedded single quote (SQL convention; same rule SQLite and PostgreSQL use)
* **Operators** — `= != < <= > >= AND OR NOT LIKE`
* **Punctuation** — `( ) , *`
* **Keywords** (case-insensitive) — `SELECT FROM WHERE ORDER BY ASC DESC LIMIT COUNT`

The single `==`/`!=` distinction is handled at the lexer level: `=` is one token, `<`/`>`/`!` look ahead one character to decide between `<`/`<=`, `>`/`>=`, `!`/`!=`.

---

## 4. Evaluating WHERE on a row

`run_rpn(rpn, schema, row)` walks the RPN token list with a single stack of `Eval` values. Each `Eval` is one of `{Int, Str, Bool}`. Identifier lookups consult `schema.find_column(name)` and pull the cell straight out of the row.

Type rules:

* Comparisons require both sides to be the same type (`Int` vs `Int` or `Str` vs `Str`) and produce a `Bool`.
* `AND` / `OR` / `NOT` require boolean operands and produce a `Bool`.
* `LIKE` requires both sides to be strings and produces a `Bool`.

A mismatch throws — we'd rather signal a bug at query time than silently coerce.

### LIKE — the `%` wildcard

The implementation is a two-pointer match with back-tracking on `%`:

```
si, pi = 0, 0
star, mark = none, 0
while si < |s|:
    if pi < |p| and p[pi] = '%':     star, mark = pi++, si
    elif pi < |p| and p[pi] = s[si]: pi++, si++
    elif star != none:               pi = star + 1; si = ++mark   (back up, try one more char)
    else: return false
consume trailing '%' on p; return pi == |p|
```

`%` matches any sequence including the empty one, so `'The %'` matches `"The Lean Startup"` and `"The Pragmatic Programmer"` but not `"Zero to One"`. SQL's `_` (single-char wildcard) is not implemented — out of scope.

---

## 5. The `Query` record + execute pipeline

```cpp
struct Query {
    std::vector<std::string> projection;     // empty ⇒ SELECT *
    bool                     is_count_star;
    std::string              table;
    std::vector<Token>       where_rpn;      // empty ⇒ no filter
    std::string              order_col;
    bool                     order_desc;
    std::int64_t             limit;          // -1 ⇒ no LIMIT
};
```

`run()`:

1. Decides result columns: source columns / explicit projection / single `count` column for COUNT(\*).
2. Sweeps `src.rows` and keeps a `vector<const Row*>` of survivors (no copy until the very end).
3. Sorts the survivor pointers if ORDER BY is present.
4. Truncates to LIMIT.
5. Builds the result rows by projecting the kept pointers, or computes the aggregate.

Keeping pointers in step 2 makes the predicate the only thing we pay for per row — no per-row allocation, no copy until the final projection.

---

## 6. Demo output (selected sections)

```
=== Part 2b) WHERE with AND / OR / parentheses + projection + ORDER BY DESC ===
SQL: SELECT title, year, pages FROM books WHERE year >= 2000 AND (genre = 'Tech' OR pages > 400) ORDER BY pages DESC
+---------------------------------------+------+-------+
| title                                 | year | pages |
+---------------------------------------+------+-------+
| Designing Data Intensive Applications | 2017 | 616   |
| Clean Code                            | 2008 | 464   |
| Homo Deus                             | 2015 | 450   |
| Sapiens                               | 2011 | 443   |
| Don't Make Me Think                   | 2014 | 216   |
+---------------------------------------+------+-------+
(5 rows)

=== Part 2c) LIKE wildcard + LIMIT ===
SQL: SELECT title, author FROM books WHERE title LIKE 'The %' ORDER BY title ASC LIMIT 2
| The Lean Startup         | Eric Ries   |
| The Pragmatic Programmer | Andrew Hunt |
(2 rows)

=== Part 2e) COUNT(*) aggregation ===
| count |
| 4     |
```

Each scenario in `main.cpp` calls `must(condition, "what")` afterwards — if any answer is wrong, the run aborts with `FAIL: …`. The final line `All SQL engine checks passed.` means every assertion ran clean.

---

## 7. Public API

```cpp
using namespace adbms::sql;

Relation t = /* build columns + rows of cells */;

// Whole-statement path:
Query   q = parse("SELECT title FROM books WHERE pages > 400 ORDER BY year DESC LIMIT 3");
Relation r = run(q, t);
print(r);

// Just the predicate-compilation path, useful when iterating row-by-row:
auto rpn = to_rpn(lex("genre = 'Tech' AND pages < 500"));
for (const Row& row : t.rows)
    if (run_rpn(rpn, t, row)) { /* row matched */ }

std::cout << rpn_trace(rpn);  // debug print: "genre 'Tech' = pages 500 < AND"
```

---

## 8. Complexity

Let `n` = rows in the source table, `c` = columns, `t` = tokens in the WHERE clause.

| Step           | Cost                  |
|----------------|-----------------------|
| Lex            | `O(|sql|)`            |
| Parse skeleton | `O(t)`                |
| Shunting-yard  | `O(t)`  (single pass) |
| Per-row eval   | `O(t)`  (linear pass over RPN) |
| Total run()    | `O(n · t)` filter + `O(n log n · c)` sort if ORDER BY + `O(min(LIMIT, |kept|) · c)` project |

The predicate is **compiled once** — re-using the same `where_rpn` across the row sweep means we don't re-parse for every row. That's the same shape SQLite uses with its bytecode VM and PostgreSQL uses with its expression-evaluation tree.

---

## 9. How this implementation differs from PR #792

| Aspect | Reference (#792) | This submission |
|---|---|---|
| Layout | `sql_engine.h` + `sql_engine.cc` + `main.cc` | Header-only `sql_engine.hpp` + `main.cpp` |
| Namespace | `sqlmini` | `adbms::sql` |
| Build | `Makefile` | `CMakeLists.txt` (with `-Wshadow`) |
| `LIKE` operator | — | Implemented (`%` wildcard, with two-pointer back-tracking matcher) |
| `COUNT(*)` aggregation | — | Implemented (single-row result with column `count`) |
| Pretty-printed output | aligned, plain | aligned with `+----+` borders and a `(N rows)` footer |
| Demo dataset | employees (8 rows) | books (12 rows) |
| Scenarios | 4 | 7 — adds LIKE, COUNT(\*), and an explicit `''` escape test |

The shunting-yard algorithm itself is the same algorithm; there is no alternative version of it worth implementing. The originality is in the surface area (LIKE / COUNT), the file structure, and the test coverage.

---

## 10. Quick command reference

```bash
# Build + run with CMake
cmake -S . -B build && cmake --build build && ./build/sql_engine_demo

# Or with a single compiler call
clang++ -std=c++17 -Wall -Wextra -Wpedantic -O2 main.cpp -o sql_engine_demo && ./sql_engine_demo

# Expected final line:  All SQL engine checks passed.
```
