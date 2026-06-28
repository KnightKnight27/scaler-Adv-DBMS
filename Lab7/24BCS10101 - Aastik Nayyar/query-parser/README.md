# Mini SQL Query Engine in C++

## Overview

This project implements a minimal SQL query engine that executes SQL-like statements against an in-memory student dataset. It demonstrates the core stages of query processing: tokenization (lexical analysis), AST construction via parsing, condition evaluation, and result output.

---

## Supported Query Syntax

```sql
SELECT <column>
FROM <table>
WHERE <condition>
```

Example:

```sql
SELECT name FROM students WHERE id <= 3
```

Supported comparison and logical operators:

* `>`
* `<`
* `>=`
* `<=`
* `=`
* `OR`
* Parentheses `()` for grouping

---

## How It Works

### 1. Tokenization (Lexical Analysis)

The raw query string is scanned character by character and converted into a sequence of tokens.

Example:

```sql
SELECT name FROM students WHERE id <= 3
```

produces:

```text
SELECT | name | FROM | students | WHERE | id | <= | 3
```

The tokenizer recognizes SQL keywords, column identifiers, numeric literals, comparison operators, and parentheses.

---

### 2. Parsing and AST Construction

The parser processes the token stream using recursive descent and builds an Abstract Syntax Tree (AST) representing the WHERE condition.

For a simple condition:

```sql
id <= 3
```

the AST looks like:

```text
      <=
     /  \
   id    3
```

For compound conditions:

```sql
(id <= 3 OR age > 25)
```

the parser builds a tree capturing the logical relationship between sub-expressions.

---

### 3. Condition Evaluation

Each row in the dataset is tested against the AST. The evaluator traverses the tree recursively:

* Reads column values (`id`, `age`) from the row
* Compares them against literal constants
* Handles logical connectives like `OR`

The result is `true` or `false` for each row.

---

### 4. Query Execution

The engine iterates over all rows in the dataset:

1. Evaluate the WHERE clause for the current row.
2. If the row passes, extract the requested column value.
3. Print the result.

Example:

```sql
SELECT name FROM students WHERE id <= 3
```

Output:

```text
Arjun
Priya
Vikram
```

---

## Component Summary

| Component       | Role                                         |
| --------------- | -------------------------------------------- |
| Pupil           | Holds a single row of data                   |
| Tokenizer       | Splits query text into tokens                |
| SQLParser       | Builds an AST from the token stream          |
| AST Nodes       | Represent columns, literals, and operations  |
| Evaluator       | Recursively resolves conditions per row      |
| Query Executor  | Filters matching rows and prints output      |

---

## Concepts Covered

* Lexical Analysis (Tokenization)
* Recursive Descent Parsing
* Abstract Syntax Tree Construction
* Tree-Based Expression Evaluation
* Fundamentals of Query Processing
* In-Memory Data Filtering

This project provides a simplified look at how database engines internally parse and execute SQL queries.
