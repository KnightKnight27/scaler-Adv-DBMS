# Lab 5 — Shunting-Yard + Minimal SQL SELECT Parser

Two parts that fit together:

1. **Part 1 — Shunting-Yard**: convert an infix expression to postfix
   (RPN) and evaluate it. This is the mechanism a database uses to make
   sense of a `WHERE` clause like `age > 25 && salary * 1.1 < 90000`.
2. **Part 2 — Minimal SQL SELECT**: parse a small subset of `SELECT` and
   run it over an in-memory `vector<Row>`, using Part 1 to evaluate the
   `WHERE` condition for each row.

## Layout

```
Lab-5/
├── shunting_yard.h    # tokenize / to_rpn / eval_rpn declarations
├── shunting_yard.cc   # the shunting-yard algorithm + RPN evaluator
├── sql.h              # Row, SelectQuery, parse_select, execute
├── sql.cc             # parser + filter/project/sort/limit executor
├── main.cc            # demo: expression cases + SELECT queries
├── Makefile           # g++ -std=c++17 -Wall -Wextra -O2
└── README.md
```

## Build & run

```bash
cd Lab-5
make          # builds ./sqlmini
make run      # builds and runs the demo
make clean
```

Without `make`:

```bash
g++ -std=c++17 -Wall -Wextra -O2 main.cc shunting_yard.cc sql.cc -o sqlmini
./sqlmini
```

## Part 1 — How shunting-yard works

The algorithm reads infix tokens left to right and uses one operator
stack to reorder them into postfix:

- **number / identifier** → send straight to the output.
- **operator** → while the operator on top of the stack binds at least
  as tightly (higher precedence, or equal precedence and left
  associative), pop it to the output; then push the current operator.
- **`(`** → push. **`)`** → pop until the matching `(`.

At the end, pop whatever operators remain. The result is RPN, which a
stack evaluates in one pass: push operands, and on each operator pop two,
apply, push the result.

### Precedence table (highest binds tightest)

| Level | Operators | Notes |
| ----- | --------- | ----- |
| 6 | `*` `/` | arithmetic |
| 5 | `+` `-` | arithmetic |
| 4 | `<` `>` `<=` `>=` | comparison |
| 3 | `=` `!=` | equality |
| 2 | `&&` | logical AND |
| 1 | `\|\|` | logical OR |

Putting comparison/logical *below* arithmetic is what makes
`age * 2 + 10 > 60` parse as `(age * 2 + 10) > 60` — exactly how a SQL
`WHERE` would read it. Comparisons and logical operators produce `1`
(true) or `0` (false), so they chain naturally.

Example from the demo:

```
expr : age * 2 + 10 > 60
rpn  : age 2 * 10 + 60 >
value: 1                     (with age = 30)
```

## Part 2 — The minimal SELECT engine

`parse_select` understands:

```
SELECT <col, col, ... | *>
FROM   <table>
[WHERE  <expression>]
[ORDER BY <col> [ASC|DESC]]
[LIMIT  <n>]
```

`execute` then runs the classic four steps in order:

```
filter (WHERE)  →  project (SELECT cols)  →  sort (ORDER BY)  →  truncate (LIMIT)
```

- **Filter**: the WHERE text is tokenized and converted to RPN **once**,
  then evaluated per row with that row's columns supplied as variables.
- **Project**: keep only the requested columns (or every column for `*`).
- **Sort**: stable sort on the `ORDER BY` column, ascending or descending.
- **Truncate**: cut the result down to `LIMIT` rows.

A `Row` is just `unordered_map<string, Value>` where `Value` is a
`variant<double, string>`, so both numeric and text columns are supported
(WHERE conditions are numeric in this toy).

## How this maps to a real database

```
SQL text
   │  tokenize()
Tokens
   │  parse_select()        → SelectQuery (a tiny AST)
Query plan (implicit here: filter → project → sort → limit)
   │  execute()
Result rows
```

In a real engine the WHERE expression is compiled into an expression
tree by the planner (shunting-yard is one way to build that tree), the
planner decides whether to use an index or a full scan, and the executor
streams rows instead of materialising a `vector`. The `vector<Row>` here
stands in for the output of a storage layer that already fetched the
pages — the same layer Lab 3 (buffer pool) and Lab 4 (B-tree index)
implement.

## Key takeaways

- Shunting-yard converts infix to RPN in one O(n) pass with a single
  stack — no recursion, no parse tree needed to evaluate.
- Precedence + associativity are the only two rules that decide the
  output order.
- A minimal SQL executor really is just filter → project → sort → limit.
- Compiling the WHERE clause once and reusing it per row is the small but
  important optimisation that separates "re-parse every row" from how a
  real planner works.
