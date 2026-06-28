# SQL WHERE Clause to Postfix Converter using Shunting-Yard

## Overview

This project demonstrates how a SQL `WHERE` clause expressed in standard **infix notation** can be converted into **postfix notation (Reverse Polish Notation)** using Dijkstra's **Shunting-Yard Algorithm**. The resulting postfix expression is then evaluated against an in-memory student dataset to identify matching rows.

---

## How It Works

### 1. Tokenization

The input condition string is broken into individual tokens:

* Column names (`id`, `age`)
* Numeric constants (`3`, `25`)
* Operators (`>`, `<`, `>=`, `AND`, `OR`)
* Parentheses for grouping

Example:

```sql
id > 3 AND (age < 25 OR age >= 30)
```

---

### 2. Infix to Postfix Conversion

The Shunting-Yard Algorithm uses two data structures:

* An **output queue** that accumulates the postfix result
* An **operator stack** that manages precedence and parentheses

Operator precedence levels:

```text
Comparison (>, <, >=, <=, =)   -> Highest (3)
AND                             -> Medium  (2)
OR                              -> Lowest  (1)
```

Example conversion:

```text
Infix:
id > 3 AND (age < 25 OR age >= 30)

Postfix (RPN):
id 3 > age 25 < age 30 >= OR AND
```

---

### 3. Postfix Evaluation via Stack

The RPN expression is evaluated using a stack-based approach:

1. Push operands (column values or constants) onto the stack.
2. When an operator is encountered, pop the required operands.
3. Apply the operation and push the result back onto the stack.

The final value on the stack indicates whether the row satisfies the WHERE clause.

---

## Processing Pipeline

```text
WHERE Clause (infix)
      |
Tokenization
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

This project offers a practical look at how query engines internally convert and evaluate logical expressions prior to filtering data.
