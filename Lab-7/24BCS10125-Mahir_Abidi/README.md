# Lab 7 — SQL WHERE Clause Evaluation: Two Approaches

> **Course:** Advanced DBMS
> **Author:** Mahir Abidi
> **Roll No:** 24BCS10125
> **Language:** C++17

This lab implements two distinct strategies for evaluating SQL `WHERE`
expressions against an in-memory dataset:

1. **`dijkstraShunting/`** — Uses Dijkstra's Shunting-Yard algorithm to
   convert an infix WHERE expression into postfix (RPN), then evaluates
   it row by row using a stack.
2. **`queryParsing/`** — Uses a recursive-descent parser to build an AST
   (`std::unique_ptr<Node>`), then traverses the tree for each row.

Both approaches solve the same problem — they just reflect two fundamental
techniques for expression evaluation in query engines.

## Project Structure

```
Lab-7/24BCS10125-Mahir_Abidi/
├── dijkstraShunting/main.cpp   # infix -> postfix conversion + stack eval
├── queryParsing/main.cpp       # recursive-descent parser + AST walker
├── Makefile                    # `make`, `make run`, `make clean`
└── README.md
```

## Building & Running

```bash
make              # compiles both binaries
make run          # executes both and prints results
make clean        # removes compiled binaries
```

To run individually: `make run-shunting` or `make run-parser`.

## Feature Comparison

| Feature | Shunting-Yard | Recursive-descent |
| --- | --- | --- |
| Column identifiers (`id`, `age`)    | ✓ | ✓ |
| Integer literals                    | ✓ | ✓ |
| Comparisons `> < >= <= = !=`        | ✓ | ✓ |
| Logical `AND` / `OR`                | ✓ | ✓ |
| Parenthesized sub-expressions       | ✓ | ✓ |
| Correct operator precedence         | ✓ | ✓ (enforced by grammar) |
| Full `SELECT col FROM tbl WHERE ...`| — | ✓ |

## Grammar

The recursive-descent parser follows this grammar:

```
query      := SELECT <ident> FROM <ident> WHERE <expr>
expr       := orExpr
orExpr     := andExpr ( OR  andExpr )*
andExpr    := primary ( AND primary )*
primary    := '(' expr ')' | comparison
comparison := <ident> <op> <number>
op         := > | < | >= | <= | = | !=
```

Precedence is baked into the grammar levels: comparisons bind tightest,
then `AND`, then `OR` — no separate precedence table needed.

## Example Queries

```sql
SELECT name FROM employees WHERE id >= 3 OR age < 20
SELECT name FROM employees WHERE id > 3 AND age >= 30
SELECT id   FROM employees WHERE (age < 25 AND id != 2) OR age >= 30
```

## Design Decisions

- **No `using namespace std;`** — avoids polluting the global namespace.
- **No `<bits/stdc++.h>`** — only standard, portable headers included.
- **Memory safety** — AST nodes are managed via `std::unique_ptr`; no raw `new`.
- **Error handling** — all parse and evaluation errors throw `std::runtime_error`;
  `main()` catches and exits with a non-zero status.
- **Compiler warnings** — code is clean under `-Wall -Wextra -Wpedantic -Wshadow`.

## Approach Comparison

Shunting-Yard works well when the input is a flat stream of tokens with
defined precedence levels — exactly what a SQL `WHERE` clause looks like.
Recursive descent works better when the input has hierarchical structure
like full SQL statements with keywords and clauses. Production databases
typically use recursive descent at the statement level and precedence
climbing for inner expressions — this lab demonstrates each technique
independently.
