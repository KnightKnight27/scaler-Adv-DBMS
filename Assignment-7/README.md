# Assignment 7 — Query Engine (Shunting-Yard + Recursive-Descent Parser)

**Tanishq Singh | 24BCS10303**

---

Two implementations of a SQL WHERE clause evaluator, solving the same problem
in two different ways:

## Part A — Dijkstra's Shunting-Yard (`main_dsy.cpp`)

Converts a WHERE clause from infix to Reverse Polish Notation (postfix) using
an operator stack and a precedence table, then evaluates the postfix expression
with a single integer stack to filter rows. No recursion at eval time.

See `README_dsy.md` for a full algorithm walkthrough with a step-by-step trace.

## Part B — Recursive-Descent Parser + AST (`main_queryparser.cpp`)

Full SQL `SELECT ... FROM ... WHERE ...` parser. Lexes the input into tokens,
builds an AST via recursive descent (grammar encodes operator precedence),
then walks the tree once per row to decide which rows pass the filter.

See `README_queryparser.md` for the grammar and AST details.

---

## Build & Run

```bash
# Part A — Shunting-Yard
g++ -std=c++17 -Wall -Wextra -o dsy main_dsy.cpp && ./dsy

# Part B — Recursive-Descent
g++ -std=c++17 -Wall -Wextra -o queryparser main_queryparser.cpp && ./queryparser
```

Both build clean with no warnings.
