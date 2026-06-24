# Lab 5 — Shunting-Yard Expression Evaluator + SQL SELECT Parser

## Overview

This lab implements two interconnected components:

1. **Dijkstra's Shunting-Yard Algorithm** — Converts infix expressions to postfix (RPN) and evaluates them
2. **Minimal SQL SELECT Parser** — Parses and executes SQL SELECT statements over in-memory `vector<Row>`

The SQL parser uses the Shunting-Yard evaluator for WHERE clause evaluation.

## Part 1: Shunting-Yard Algorithm

### How It Works

The Shunting-Yard algorithm converts infix notation (e.g., `3 + 4 * 2`) to postfix notation (e.g., `3 4 2 * +`) using two data structures:

```
Input tokens → [Output Queue]
             → [Operator Stack]

Rules:
  Number/Variable  → push to output
  Operator         → pop higher-precedence ops to output, then push
  Left Paren       → push to stack
  Right Paren      → pop to output until left paren
```

### Operator Precedence

| Precedence | Operators | Associativity |
|-----------|-----------|---------------|
| 7 (highest) | `*`, `/`, `%` | Left |
| 6 | `+`, `-` | Left |
| 5 | `<`, `>`, `<=`, `>=` | Left |
| 4 | `==`, `!=` | Left |
| 3 | `NOT` | Right |
| 2 | `AND` | Left |
| 1 (lowest) | `OR` | Left |

### Example

```
Infix:   3 + 4 * 2
Step 1:  output=[3], stack=[]
Step 2:  output=[3], stack=[+]
Step 3:  output=[3, 4], stack=[+]
Step 4:  output=[3, 4], stack=[+, *]    (* has higher precedence, push)
Step 5:  output=[3, 4, 2], stack=[+, *]
End:     output=[3, 4, 2, *, +]         (pop remaining)
Result:  3 4 2 * + = 3 + 8 = 11
```

## Part 2: SQL SELECT Parser

### Supported Syntax

```sql
SELECT col1, col2, ...  |  SELECT *
FROM table_name
[WHERE condition]
[ORDER BY column [ASC|DESC]]
[LIMIT n]
```

### WHERE Clause Operators
- Comparison: `=`, `!=`, `<`, `>`, `<=`, `>=`
- Logical: `AND`, `OR`, `NOT`
- String comparison: `column == 'value'`

### Architecture

```
SQL String
  │
  ▼
SQLTokenizer → [SQLToken, SQLToken, ...]
  │
  ▼
SQLParser → SelectStatement { columns, table, where, order_by, limit }
  │
  ▼
SQLExecutor → for each Row:
                1. Evaluate WHERE using ShuntingYard
                2. Project selected columns
                3. Sort by ORDER BY
                4. Apply LIMIT
  │
  ▼
vector<Row> (results)
```

## Building and Running

```bash
make        # compile
make run    # compile and run
make clean  # cleanup
```

## Files

| File | Description |
|------|-------------|
| `shunting_yard.h` | Tokenizer + Shunting-Yard algorithm + evaluator |
| `sql_parser.h` | SQL tokenizer + parser + executor |
| `main.cpp` | Driver with expression and SQL tests |
| `Makefile` | Build targets |
