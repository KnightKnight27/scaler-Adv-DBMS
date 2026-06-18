# Lab 5 — Shunting-Yard Algorithm + Minimal SQL SELECT Parser

## Overview

Two programs in C++17:

| Program | Source | What it does |
|---|---|---|
| `shunting_yard` | [`djikstra/main.cpp`](djikstra/main.cpp) | Dijkstra's Shunting-Yard: infix expression → RPN, then evaluates against an in-memory `Employee` table |
| `sql_parser` | [`query/main.cpp`](query/main.cpp) | Minimal SQL parser for `SELECT col FROM tbl WHERE …` that runs the WHERE clause through the shunting-yard evaluator |

## Part 1 — Shunting-Yard (`djikstra/main.cpp`)

The shunting-yard algorithm converts infix expressions into Reverse Polish
Notation (postfix) using a stack. The output RPN can be evaluated by a
simple stack machine.

### Operators supported

| Token | Precedence | Notes |
|---|---|---|
| `OR` | 1 | logical |
| `AND` | 2 | logical |
| `=`, `!=` | 3 | equality |
| `<`, `>`, `<=`, `>=` | 4 | comparison |
| `(`, `)` | — | grouping |

Two-character operators (`<=`, `>=`, `!=`) and C-style boolean operators
(`&&`, `||`) are both handled at the lex layer; `&&` and `||` are
normalized to `AND` / `OR` so the precedence table stays simple.

### How it works

1. **`tokenize(expr)`** — splits the input string into numbers, identifiers, and operators.
2. **`toPostfix(tokens)`** — runs the actual shunting-yard pass:
   - number/identifier → output
   - `(` → push to op-stack
   - `)` → pop until `(`
   - operator → pop higher-or-equal precedence ops, then push
3. **`evaluatePostfix(rpn, emp)`** — stack machine. Numbers/identifiers
   are pushed; operators pop two values and push the result.

### Demo

The demo is interactive — it prompts for a SQL `WHERE` clause, prints
the tokens, prints the postfix, then prints every `Employee` row that
matches.

```text
$ ./shunting_yard
Enter SQL Query:
SELECT * FROM employees WHERE id >= 3 OR age < 20

WHERE Clause:
id >= 3 OR age < 20

Tokens:
id >= 3 OR age < 20

Postfix Expression:
id 3 >= age 20 < OR

Matching Employees:

ID: 3 | Age: 19 | Name: Karan
ID: 4 | Age: 21 | Name: Sneha
ID: 5 | Age: 20 | Name: Vivaan
ID: 6 | Age: 31 | Name: Ishaan
ID: 7 | Age: 22 | Name: Meera
ID: 8 | Age: 33 | Name: Devansh
```

## Part 2 — SQL SELECT Parser (`query/main.cpp`)

A small, hand-written parser that recognizes:

```sql
SELECT <col> FROM <tbl> WHERE <expr>
```

It runs `<expr>` through the same shunting-yard converter and evaluates
the RPN against each row in the in-memory `vector<Employee>` table.

### Demo

```text
$ ./sql_parser
Query: SELECT name FROM employees WHERE id >= 3 OR age < 20
Karan
Sneha
Vivaan
Ishaan
Meera
Devansh

Query: SELECT name FROM employees WHERE id > 3 AND age >= 30
Ishaan
Devansh

Query: SELECT id FROM employees WHERE ( age < 25 AND id != 2 ) OR age >= 30
1
3
4
5
6
7
8
```

## Build

```bash
make            # builds both shunting_yard and sql_parser
make run-shunting
make run-sql
make clean
```

Both compile clean under `-std=c++17 -Wall -Wextra`.

## How this connects to a real database

```
SQL string
   |
Lexer / Tokenizer      <- tokenize()
   |
Parser                 <- parseQuery()  produces a ParsedQuery
   |
Executor               <- evaluatePostfix() runs against vector<Employee>
   |
Result set
```

In a real DB the WHERE expression is compiled into an expression tree
by the planner; shunting-yard is the mechanism that builds that tree.
The `vector<Employee>` here stands in for what a storage layer would
have already fetched from disk (the pages of Labs 2-3).

## Author

24BCS10335 — Swaim Sahay
