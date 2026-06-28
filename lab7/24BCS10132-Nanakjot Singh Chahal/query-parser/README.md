# Lightweight SQL Query Processor in C++

## Overview

This project builds a basic SQL query processor that runs SQL-like statements against an in-memory student dataset. It walks through the fundamental stages of query handling: scanning (lexical analysis), parsing into an AST, evaluating filter conditions, and producing output.

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

### 1. Scanning (Lexical Analysis)

The raw query string is scanned character by character and split into a sequence of lexemes (tokens).

Example:

```sql
SELECT name FROM students WHERE id <= 3
```

produces:

```text
SELECT | name | FROM | students | WHERE | id | <= | 3
```

The scanner recognizes SQL keywords, identifiers, numeric constants, comparison operators, and parentheses.

---

### 2. Parsing and AST Construction

The parser processes the lexeme stream using recursive descent and constructs an Abstract Syntax Tree (AST) representing the WHERE condition.

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

the parser builds a tree that captures the logical structure between sub-expressions.

---

### 3. Condition Evaluation

Each student record is tested against the AST. The evaluator walks the tree recursively:

* Reads column values (`id`, `age`) from the record
* Compares them against literal constants
* Handles logical connectives like `OR`

The result is `true` or `false` for each row.

---

### 4. Query Execution

The engine loops over all student records:

1. Evaluate the WHERE clause for the current row.
2. If the row passes, extract the requested column.
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

| Component        | Role                                        |
| ---------------- | ------------------------------------------- |
| Student          | Holds a single row of data                  |
| Scanner          | Breaks query text into lexemes              |
| QueryParser      | Constructs an AST from the lexeme stream    |
| AST Nodes        | Represent fields, literals, and operations  |
| Evaluator        | Recursively resolves conditions per row     |
| Query Executor   | Filters matching rows and prints output     |

---

## Concepts Covered

* Lexical Analysis (Scanning)
* Recursive Descent Parsing
* Abstract Syntax Tree construction
* Tree-based Expression Evaluation
* Fundamentals of Query Processing
* In-Memory Data Filtering

This project offers a simplified look at how database engines internally break down and execute SQL queries.
