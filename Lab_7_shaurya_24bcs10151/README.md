# Lab 7 — SQL `WHERE` Parsing & Evaluation

**Name:** Shaurya Verma
**Roll No.:** 24BCS10151

## Goal

Take a single SQL query —

```sql
SELECT name FROM students WHERE marks >= 80 AND (age < 20 OR id = 5)
```

— and parse and evaluate its `WHERE` clause **two different ways**, then
run both against the same in-memory `students` table and confirm they
return identical rows. The point of doing it twice is to see where
operator precedence actually lives: once as **data** (a precedence
lookup table) and once as **control flow** (the shape of the parser's
own call graph).

## Layout

```
Lab_7_shaurya_24bcs10151/
├── README.md                  ← this file
├── shunting_yard/
│   ├── main.cpp               ← infix → RPN + int-stack evaluator
│   └── Makefile               ← make / make run / make clean
└── recursive_descent/
    ├── main.cpp               ← lexer + AST parser + tree-walk evaluator
    └── Makefile               ← make / make run / make clean
```

## The two implementations

### 1. Shunting-yard — `shunting_yard/main.cpp`

Dijkstra's shunting-yard algorithm. The tokenised clause flows left to
right: operands go straight to an output list, operators wait on a stack
until something of equal-or-higher precedence pops them off. The result
is the clause in **Reverse Polish Notation**, which a second one-pass
walk evaluates with a plain `int` stack — push values, pop two and apply
on each operator.

```text
Infix WHERE : marks >= 80 AND (age < 20 OR id = 5)
Postfix RPN : marks 80 >= age 20 < id 5 = OR AND
```

Precedence is one small table and nothing else:

| Operator                  | Precedence |
|---------------------------|------------|
| `OR`                      | 1 |
| `AND`                     | 2 |
| `=`, `<`, `>`, `<=`, `>=` | 3 |

By the time the evaluator runs, the parentheses are gone — they did
their work during the infix→RPN conversion. No recursion, no heap nodes.

### 2. Recursive-descent — `recursive_descent/main.cpp`

A handwritten lexer plus three mutually-recursive functions:

```text
parseOr      → parseAnd    ( OR  parseAnd    )*
parseAnd     → parseCompare ( AND parseCompare )*
parseCompare → '(' parseOr ')'  |  Ident CmpOp Number
```

Because `parseOr` calls `parseAnd` calls `parseCompare`, the grammar's
call graph *is* the precedence rule — there is no precedence table in
the file at all. The functions build a small AST that gets walked
recursively at evaluation time:

```text
AND
  >=
    marks
    80
  OR
    <
      age
      20
    =
      id
      5
```

Interior nodes are boolean operators (`AND`/`OR`); leaves are
`column op literal` comparisons.

## Build & run

Each folder has its own `Makefile` with `make` (build), `make run`
(build + run), and `make clean` (remove the binary):

```bash
cd shunting_yard     && make run
cd recursive_descent && make run
```

Both compile with `g++ -std=c++17 -Wall -Wextra -O2` and produce no
warnings.

## The test table

| id | name  | age | marks |
|----|-------|-----|-------|
| 1  | Priya | 19  | 88    |
| 2  | Rohan | 22  | 67    |
| 3  | Sneha | 20  | 91    |
| 4  | Arjun | 23  | 74    |
| 5  | Meera | 21  | 95    |
| 6  | Karan | 18  | 59    |

Working the clause out by hand:

- `marks >= 80` holds for **Priya, Sneha, Meera**.
- `age < 20 OR id = 5` holds for **Priya** (age 19), **Karan** (age 18),
  and **Meera** (id 5).
- The `AND` keeps only the intersection: **Priya** and **Meera**.

## Captured output

### Shunting-yard

```text
Lab 7 - shunting-yard (Shaurya Verma, 24BCS10151)

Infix WHERE : marks >= 80 AND (age < 20 OR id = 5)
Postfix RPN : marks 80 >= age 20 < id 5 = OR AND

Matching rows (SELECT name ...):
  Priya  (id=1, age=19, marks=88)
  Meera  (id=5, age=21, marks=95)
```

### Recursive-descent

```text
Lab 7 - recursive-descent parser (Shaurya Verma, 24BCS10151)

Query: SELECT name FROM students WHERE marks >= 80 AND (age < 20 OR id = 5)

WHERE as an AST (precedence encoded in tree shape):
AND
  >=
    marks
    80
  OR
    <
      age
      20
    =
      id
      5

Matching rows (SELECT name FROM students):
  Priya
  Meera
```

Both return **Priya** and **Meera** — the two parsers agree, which is
the cross-check the lab is after.

## Side-by-side comparison

| Aspect                | Shunting-yard                       | Recursive-descent |
|-----------------------|-------------------------------------|-------------------|
| Output shape          | Flat token list (RPN)               | Tree (AST) |
| Precedence lives in   | Lookup table                        | Grammar / call graph |
| Memory                | Two `std::vector`s, no heap nodes   | One `unique_ptr<Node>` per AST node |
| Recursion             | None                                | Yes — one frame per nesting level |
| Easiest to extend with| New operators (add a table row)     | New node kinds, optimisation passes |
| Real-world analogue   | Calculators, spreadsheet formulas   | PostgreSQL, MySQL, compilers |

Neither is strictly better. Shunting-yard wins when you only need to
*evaluate* an expression and throw it away. Recursive-descent wins the
moment you want to *keep* the structure — print an `EXPLAIN`, reorder
`AND` operands by cost, push predicates into an index scan, type-check.
Real database engines parse into an AST precisely because every stage
after parsing (rewrite, plan, cost, execute) wants tree access.

## What I took away

- **Precedence is the entire problem.** Strip it out and both parsers
  are trivial. Shunting-yard expresses it as a table; recursive-descent
  expresses it as the order in which functions call one another. Same
  idea, two encodings.
- **RPN is decision-free to evaluate.** Once the clause is in postfix,
  the evaluator just pushes operands and applies operators — no
  branching on precedence, no parentheses to track.
- **An AST is worth the extra code when you need to reason about the
  query later.** The recursive-descent version is longer, but it hands
  you a structure you can inspect, rewrite, and optimise — which is
  exactly what a real DBMS needs.
