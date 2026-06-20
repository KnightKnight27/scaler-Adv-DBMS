# Lab 7: Dijkstra's Shunting-Yard Expression Evaluator + Minimal SQL SELECT Parser

## 🛠️ Compilation & Execution

```bash
g++ -std=c++17 shunting_yard_sql.cpp -o shunting_yard_sql
./shunting_yard_sql
```

---

## 📋 Overview

This lab implements two interconnected components that demonstrate how database engines parse and evaluate expressions:

### Part A — Shunting-Yard Algorithm (Infix → Postfix)

Dijkstra's **Shunting-Yard algorithm** converts infix expressions into postfix (Reverse Polish Notation) using an operator stack and output queue. The implementation supports:

| Category    | Operators                    |
|-------------|------------------------------|
| Arithmetic  | `+`  `-`  `*`  `/`  `%`     |
| Comparison  | `=`  `!=`  `<`  `>`  `<=`  `>=` |
| Logical     | `AND`  `OR`  `NOT`           |
| Grouping    | `(`  `)`                     |

Each conversion is shown **step-by-step**, displaying the token being processed, the action taken (push to output / push to operator stack / pop), and the current state of the output queue.

### Part B — Minimal SQL SELECT Parser over `vector<Row>`

A hand-written recursive-descent SQL parser supporting:

```sql
SELECT <columns | *> FROM <table> [WHERE <expr>] [ORDER BY <col> [ASC|DESC]]
```

- **`SELECT *`** or specific column projection
- **`WHERE` clause** — the expression is tokenized, converted to postfix via Shunting-Yard, and evaluated row-by-row against a `vector<Row>` (where `Row = unordered_map<string, Value>`)
- **`ORDER BY`** with `ASC` / `DESC`
- String literals (single-quoted), numbers, and column references
- Boolean connectives (`AND`, `OR`, `NOT`) and comparisons

### Part C — Interactive SQL REPL

After the automatic demo, an interactive prompt (`sql>`) lets you type:

- **SQL queries** — e.g. `SELECT name FROM employees WHERE age > 30`
- **Arithmetic expressions** — e.g. `3 + 4 * 2 / (1 - 5)` → evaluates directly

---

## 🖥️ Sample Output

```text
━━━ PART A: Shunting-Yard Algorithm Demonstrations ━━━

╔══════════════════════════════════════════════════════════╗
║  Shunting-Yard: Infix → Postfix (RPN) Conversion       ║
╚══════════════════════════════════════════════════════════╝
  Input (infix): 3 + 4 * 2 / ( 1 - 5 )

  Step-by-step trace:
  ------------------------------------------------------------
  Step  Token           Action              Output Queue
  ------------------------------------------------------------
  1     3               → Output            3
  2     +               → Op Stack          3
  3     4               → Output            3 4
  4     *               → Op Stack          3 4
  5     2               → Output            3 4 2
  6     /               → Op Stack          3 4 2 *
  7     (               → Op Stack          3 4 2 *
  8     1               → Output            3 4 2 * 1
  9     -               → Op Stack          3 4 2 * 1
  10    5               → Output            3 4 2 * 1 5
  11    )               Pop until (         3 4 2 * 1 5 -
  12    /               Flush stack         3 4 2 * 1 5 - /
  13    +               Flush stack         3 4 2 * 1 5 - / +
  ------------------------------------------------------------
  Output (postfix/RPN): 3 4 2 * 1 5 - / +
  Evaluated result:    1

━━━ PART B: SQL SELECT Parser over vector<Row> ━━━

── Query: SELECT name, salary FROM employees WHERE salary > 80000 ──
+-------+--------+
| name  | salary |
+-------+--------+
| Alice | 95000  |
| Bob   | 88000  |
| Diana | 102000 |
| Hank  | 115000 |
+-------+--------+
4 row(s) returned.
```

---

## 🏗️ Architecture

```
┌──────────┐     ┌───────────────┐     ┌───────────────┐     ┌──────────┐
│  Input   │ ──▶ │   Tokenizer   │ ──▶ │ Shunting-Yard │ ──▶ │ Postfix  │
│ (infix)  │     │ (lexer)       │     │ (infix→RPN)   │     │ Evaluator│
└──────────┘     └───────────────┘     └───────────────┘     └──────────┘
                        │
                        ▼
                 ┌───────────────┐     ┌───────────────┐
                 │  SQL Parser   │ ──▶ │  SQL Executor │ ──▶ Result Table
                 │ (SELECT/FROM/ │     │ (filter, sort,│
                 │  WHERE/ORDER) │     │  project rows)│
                 └───────────────┘     └───────────────┘
```

## 📊 Data Model

- **`Value`** — variant type holding either a `double` or `string`
- **`Row`** — `unordered_map<string, Value>` (column name → value)
- **`Table`** — named collection with ordered column names + `vector<Row>`
- **`SQLQuery`** — parsed representation of a SELECT statement
