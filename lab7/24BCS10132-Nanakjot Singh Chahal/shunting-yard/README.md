# SQL WHERE Clause Postfix Converter using Shunting-Yard Algorithm

## Overview

This project shows how a SQL `WHERE` clause written in standard **infix notation** can be transformed into **postfix notation (Reverse Polish Notation)** using Dijkstra's **Shunting-Yard Algorithm**. The resulting postfix expression is then evaluated against an in-memory student dataset to filter matching rows.

---

## How It Works

### 1. Symbol Extraction (Tokenization)

The input condition string is broken into individual symbols (tokens):

* Column identifiers (`id`, `age`)
* Numeric constants (`3`, `25`)
* Operators (`>`, `<`, `>=`, `AND`, `OR`)
* Parentheses for grouping

Example:

```sql
id > 3 AND (age < 25 OR age >= 30)
```

---

### 2. Infix to Postfix Transformation

The Shunting-Yard Algorithm uses two data structures:

* A **result queue** that accumulates the postfix output
* A **holding stack** that manages operator precedence and parentheses

Operator precedence levels:

```text
Comparison (>, <, >=, <=, =)   -> Highest (3)
AND                             -> Medium  (2)
OR                              -> Lowest  (1)
```

Example transformation:

```text
Infix:
id > 3 AND (age < 25 OR age >= 30)

Postfix (RPN):
id 3 > age 25 < age 30 >= OR AND
```

---

### 3. Postfix Evaluation via Stack

The RPN expression is evaluated using a simple stack-based approach:

1. Push operands (column values or constants) onto the stack.
2. When an operator appears, pop the required operands.
3. Perform the operation and push the result back.

The final stack value indicates whether a given row satisfies the WHERE clause.

---

## Processing Pipeline

```text
WHERE Clause (infix)
      |
Symbol Extraction
      |
Shunting-Yard Algorithm
      |
Postfix Expression (RPN)
      |
Stack-Based Evaluation
      |
Filtered Rows
```

---

## Concepts Covered

* Lexical Analysis (Tokenization)
* Expression Parsing
* Dijkstra's Shunting-Yard Algorithm
* Reverse Polish Notation (RPN)
* Stack-Based Expression Evaluation
* SQL WHERE Clause Processing

This project provides a hands-on demonstration of how query engines internally convert and evaluate logical expressions before filtering data.
