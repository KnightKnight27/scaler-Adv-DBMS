# Lab 7 — Query Parsing (Shunting-Yard + Mini SQL SELECT)

**Name:** Akshat Kushwaha
**Roll No:** 24BCS10060

## What this lab is about

When a database gets a query like `SELECT ... WHERE age > 20 AND gpa >= 8`, it has
to (a) understand the `WHERE` expression and (b) run it against the rows. This lab
builds a tiny version of both:

- **Part 1 — Shunting-Yard:** convert an infix expression into **postfix (RPN)**
  so it can be evaluated with a simple stack, respecting precedence and brackets.
- **Part 2 — Mini SQL runner:** parse a small `SELECT ... FROM ... WHERE ...
  ORDER BY ... LIMIT ...` and execute it over an in-memory list of rows, using
  Part 1 to test the `WHERE` clause on each row.

## Files

| File | What it does |
|---|---|
| `query_engine.cpp` | tokenizer + shunting-yard + RPN evaluator + tiny SELECT parser/executor + demo |

## Build & run

```bash
g++ -std=c++17 -Wall -Wextra query_engine.cpp -o query_engine
./query_engine
```

Output:

```
infix : age > 20 AND (gpa >= 8 OR id == 3)
RPN   : age 20 > gpa 8 >= id 3 == OR AND

SQL: SELECT id, gpa FROM students WHERE age > 20 AND gpa >= 8 ORDER BY gpa DESC
  id=1  gpa=9
  id=5  gpa=9
  id=4  gpa=8

SQL: SELECT * FROM students WHERE gpa < 8 OR id == 5
  gpa=7  age=19  id=2
  gpa=6  age=25  id=3
  gpa=9  age=23  id=5
```

## Part 1 — why convert infix to postfix?

Infix is how we write expressions (`2 + 3 * 4`), but it's ambiguous to a computer
without precedence rules. Postfix / RPN (`2 3 4 * +`) has **no brackets and no
precedence to remember** — the order of tokens already says what to do first, so
a single stack evaluates it in one pass:

```
read 2 -> push        [2]
read 3 -> push        [2 3]
read 4 -> push        [2 3 4]
read * -> pop 4,3 push 12   [2 12]
read + -> pop 12,2 push 14  [14]
answer = 14
```

The same idea applies to SQL: `age > 20 AND gpa >= 8` needs precedence rules
(comparisons before `AND`, `AND` before `OR`) just like `*` before `+`.

### The algorithm

Keep an **output list** and an **operator stack**. For each token:

| Token | Action |
|---|---|
| operand (number / column) | push straight to the output |
| operator | while the stack top is an operator with precedence ≥ this one, pop it to output; then push this one |
| `(` | push it on the stack |
| `)` | pop to output until a `(`, then discard the `(` |

At the end pop everything left on the stack to the output. My precedence table:
comparisons = 3, `AND` = 2, `OR` = 1.

### Evaluating the RPN per row

`eval_rpn` walks the postfix list. A number pushes itself; a column name pushes
that row's value; an operator pops two values and pushes the result (comparisons
push 1/0). The single value left at the end is true/false for that row.

## Part 2 — the tiny SQL engine

`parse_select` reads the query into a small `Query` struct: the column list, the
raw `WHERE` text, an optional `ORDER BY` column + direction, and an optional
`LIMIT`. `run` then does what a real executor does, just in order:

```
filter (WHERE)  ->  project (pick columns)  ->  sort (ORDER BY)  ->  cut (LIMIT)
```

- **filter**: convert the `WHERE` text to RPN once, then evaluate it per row;
  rows that come out false are skipped.
- **project**: keep only the requested columns (or all, for `SELECT *`).
- **sort**: `std::sort` on the `ORDER BY` column, ascending or descending.
- **limit**: truncate the result.

The `students` data lives in a `vector<Row>` where each `Row` is a
`map<column, value>` — this stands in for what a storage layer would hand back
after reading pages from disk.

## How this maps to a real database

```
SQL text
   |
tokenizer        <- tokenize()
   |
WHERE -> RPN     <- to_rpn()  (a real DB builds an expression tree instead)
   |
executor         <- run(): filter, project, sort, limit
   |
result rows
```

In a real engine the `WHERE` expression is compiled once into a tree by the
planner (not re-parsed per row), and a planner would also decide whether to use
an index or a full scan. But the building blocks — tokenize, respect precedence,
evaluate per row, then project/sort/limit — are exactly these.

## Key takeaways

- Shunting-yard converts infix to postfix in one linear pass with a stack; no
  recursion needed.
- **Precedence** (and associativity) is the only thing that decides the output
  order — here, comparisons > AND > OR.
- Postfix is trivial to evaluate: numbers and columns push, operators pop two and
  push a result.
- A minimal SELECT executor is just *filter → project → sort → limit* over a list
  of rows.
