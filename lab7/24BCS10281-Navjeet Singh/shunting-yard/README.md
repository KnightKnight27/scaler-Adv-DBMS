# SQL WHERE Clause to Postfix Converter (Shunting-Yard Algorithm)

## Overview

This project demonstrates how a SQL `WHERE` clause can be converted from **infix notation** to **postfix notation (Reverse Polish Notation - RPN)** using Dijkstra's **Shunting-Yard Algorithm**. The generated postfix expression is then evaluated against an in-memory employee dataset.

---

## Logic

### 1. Tokenization

The input condition is split into tokens such as:

* Column names (`id`, `age`)
* Numeric values (`3`, `25`)
* Operators (`>`, `<`, `>=`, `AND`, `OR`)
* Parentheses

Example:

```sql
id > 3 AND (age < 25 OR age >= 30)
```

---

### 2. Infix to Postfix Conversion

The Shunting-Yard Algorithm uses:

* An **output queue** for the postfix expression
* An **operator stack** for handling precedence and parentheses

Operator precedence:

```text
Comparison Operators (>, <, >=, <=, =)  -> Highest
AND                                      -> Medium
OR                                       -> Lowest
```

Example conversion:

```text
Infix:
id > 3 AND (age < 25 OR age >= 30)

Postfix:
id 3 > age 25 < age 30 >= OR AND
```

---

### 3. Postfix Evaluation

The postfix expression is evaluated using a stack:

1. Push operands onto the stack.
2. When an operator is encountered, pop required operands.
3. Perform the operation.
4. Push the result back onto the stack.

The final value determines whether a record satisfies the WHERE clause.

---

## Workflow

```text
WHERE Clause
      ↓
 Tokenization
      ↓
 Shunting-Yard Algorithm
      ↓
 Postfix Expression (RPN)
      ↓
 Stack-Based Evaluation
      ↓
 Matching Records
```

---

## Concepts Demonstrated

* Lexical Analysis
* Expression Parsing
* Shunting-Yard Algorithm
* Reverse Polish Notation (RPN)
* Stack-Based Expression Evaluation
* SQL WHERE Clause Processing

This project provides a simplified demonstration of how query processors handle logical expressions before execution.
