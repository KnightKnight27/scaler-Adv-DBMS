# Lab 7 — Query Parsing & Shunting-Yard Evaluation

> **Course:** Advanced DBMS
> **Author:** Rama Krishnan
> **Roll No:** 24BCS10087
> **Email:** rama.24bcs10087@sst.scaler.com
> **Language:** C++17

Two complementary takes on evaluating a SQL `WHERE` clause against an
in-memory row set:

1. **`dijkstraShunting/`** — Dijkstra's Shunting-Yard algorithm. Tokenise
   the infix expression, convert to postfix (RPN), evaluate per row with a
   stack.
2. **`queryParsing/`** — Recursive-descent parser that builds an
   AST out of `unique_ptr<Node>`, then walks it per row.

Together they cover the two textbook approaches to expression evaluation in
a tiny SQL engine.

## Files

```
LAB-7/
├── dijkstraShunting/main.cpp   # Tokenise → Shunting-Yard → postfix eval
├── queryParsing/main.cpp       # Tokenise → recursive-descent AST → walk
├── Makefile                    # `make`, `make run`, `make clean`
└── README.md
```

## Build & Run

```bash
make            # builds both binaries
make run        # runs both and prints matching rows
make clean
```

Run them individually with `make run-shunting` or `make run-parser`.

## What each binary supports

| Feature | Shunting-Yard | Recursive-descent |
| --- | --- | --- |
| Identifiers (`id`, `age`)           | ✓ | ✓ |
| Integer literals                    | ✓ | ✓ |
| Comparisons `> < >= <= = !=`        | ✓ | ✓ |
| Logical `AND` / `OR` (+ `&&` / `||`) | ✓ | ✓ |
| Parentheses                         | ✓ | ✓ |
| Operator precedence (cmp > AND > OR) | ✓ | ✓ (grammar enforces it)|
| Full `SELECT col FROM tbl WHERE ...` parsing | — | ✓ |

The Shunting-Yard binary is a focused expression evaluator. The
recursive-descent binary parses the whole `SELECT` statement, including the
projected column and table name.

## Grammar (recursive descent)

```
query       := SELECT <ident> FROM <ident> WHERE <expr>
expr        := orTerm
orTerm      := andTerm ( OR  andTerm )*
andTerm     := factor  ( AND factor  )*
factor      := '(' expr ')' | comparison
comparison  := <ident> <op> <number>
op          := > | < | >= | <= | = | !=
```

The grammar encodes precedence (comparisons bind tighter than `AND`,
`AND` tighter than `OR`), so the AST is correct by construction — no
shunting yard needed on this side.

## Sample queries (run as `make run`)

```sql
SELECT name FROM employees WHERE id >= 3 OR age < 20
SELECT name FROM employees WHERE id >  3 AND age >= 30
SELECT id   FROM employees WHERE (age < 25 AND id != 2) OR age >= 30
```

## Implementation notes

- **No `using namespace std;`** — keeps the global namespace clean.
- **Standard headers only** — no `<bits/stdc++.h>`.
- **Memory safety**: AST nodes use `std::unique_ptr`; the Shunting-Yard
  path uses only `std::vector` / `std::stack`, so there's no manual `new`.
- **Error reporting**: every parse/eval failure throws
  `std::runtime_error` with a human-readable message; `main()` catches and
  returns non-zero.
- **Builds clean** under `-Wall -Wextra -Wpedantic -Wshadow`.

## Why both?

Shunting-Yard is the right shape when the expression syntax is flat (an
operator soup with precedences) — what a `WHERE` clause boils down to.
Recursive descent is the right shape when there is real structure
(keywords, clauses, sub-queries). Real databases run the recursive-descent
parser at the statement level and use Shunting-Yard-style precedence
climbing inside it for expressions — this lab shows both halves in
isolation.
