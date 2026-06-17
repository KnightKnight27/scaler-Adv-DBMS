# Lab 7 — SQL Query Parsing & Evaluation

> **Course:** Advanced DBMS
> **Author:** Krritin Keshan
> **Roll No:** 24BCS10122
> **Language:** C++17

Two side-by-side implementations of a tiny SQL engine that parses and evaluates a `WHERE` clause against a hard-coded table of students. The same query — `marks >= 80 AND (age < 20 OR id = 5)` — is run through two completely different parsing strategies so they can be compared head-to-head.

---

## Implementations

### 1. Dijkstra's Shunting-Yard — [`./shunting_yard/`](shunting_yard/)
Converts the `WHERE` clause from infix to **Reverse Polish Notation (RPN)** using an operator stack and a precedence table, then evaluates the postfix expression with a single integer stack.

- **Pros:** fast, no recursion, no tree structures — just two passes over a linear list of tokens.
- **Cons:** flat output — once flattened to RPN, the expression cannot carry structural metadata (e.g., which subtree came from which subquery).

### 2. Recursive-Descent Parser — [`./queryparsing/`](queryparsing/)
Builds an **abstract syntax tree (AST)** where precedence is baked directly into the grammar rules (OR loosest, AND tighter, comparisons tightest), then evaluates the tree with a recursive walk.

- **Pros:** structured output, easy to extend with metadata, mirrors how real DBMS query planners (Postgres, MySQL) actually work.
- **Cons:** more code; requires tree traversal and `unique_ptr` ownership management.

---

## Quick Start

Navigate into either folder and run:

```bash
make          # compile
make run      # compile and run
make clean    # remove the binary
```

Each subfolder is self-contained with its own `main.cpp`, `makefile`, and `readme.md`.

---

## At a Glance

| Feature | shunting_yard | queryparsing |
|--------|---------------|--------------|
| Parse output | Flat RPN list | AST (tree) |
| Precedence lives in | A precedence number table | Grammar rule order |
| Evaluator | One-pass integer stack | Recursive tree walk |
| Lines of code | Smaller | Larger |
| Real-world analogue | Calculators, expression evaluators | SQL planners, compilers |

---

## The Test Query

Both implementations run the same query against the same six rows so the matching set should be identical:

```sql
SELECT name FROM students WHERE marks >= 80 AND (age < 20 OR id = 5)
```

Expected matches: **Priya** (id=1, age=19, marks=88) and **Meera** (id=5, age=21, marks=95).

---

> *Submitted as part of Lab 7 — Advanced DBMS coursework.*
> **— Krritin Keshan (24BCS10122)**
