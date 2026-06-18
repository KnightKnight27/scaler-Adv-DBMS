# Mini SQL Query Engine in C++

## Overview

This project implements a simplified SQL query engine that executes SQL-like queries on an in-memory employee dataset. The goal is to demonstrate the core stages involved in query processing: tokenization, parsing, AST construction, condition evaluation, and query execution.

---

## Supported Query Format

```sql
SELECT <column>
FROM <table>
WHERE <condition>
```

Example:

```sql
SELECT name FROM employees WHERE id >= 3
```

Supported operators:

* `>`
* `<`
* `>=`
* `<=`
* `=`
* `OR`
* Parentheses `()` for grouping conditions

---

## Logic and Workflow

### 1. Lexical Analysis (Lexer)

The query string is scanned character by character and converted into a sequence of tokens.

Example:

```sql
SELECT name FROM employees WHERE id >= 3
```

becomes

```text
SELECT | name | FROM | employees | WHERE | id | >= | 3
```

The lexer identifies keywords, identifiers, numbers, comparison operators, and parentheses.

---

### 2. Parsing

The parser validates the query syntax and builds an Abstract Syntax Tree (AST) representing the WHERE condition.

For example:

```sql
id >= 3
```

is converted into:

```text
      >=
     /  \
   id    3
```

For compound conditions:

```sql
(id >= 3 OR age < 25)
```

the parser creates a tree structure that preserves the logical relationships between expressions.

---

### 3. Condition Evaluation

Each employee record is evaluated against the AST.

The evaluator recursively traverses the tree:

* Retrieves column values (`id`, `age`)
* Compares them with constants
* Resolves logical operators such as `OR`

The final result is either `true` or `false` for each row.

---

### 4. Query Execution

The engine iterates through all employee records:

1. Evaluate the WHERE condition.
2. If the condition is true, select the requested column.
3. Print the result.

Example:

```sql
SELECT name FROM employees WHERE id >= 3
```

Output:

```text
Rohan
Meera
Kabir
```

---

## Key Components

| Component      | Responsibility                       |
| -------------- | ------------------------------------ |
| Employee       | Stores row data                      |
| Lexer          | Converts query text into tokens      |
| Parser         | Builds the AST from tokens           |
| AST Nodes      | Represent expressions and conditions |
| Evaluator      | Computes condition results           |
| Query Executor | Filters rows and prints results      |

---

## Concepts Demonstrated

* Lexical Analysis
* Recursive Descent Parsing
* Abstract Syntax Trees (AST)
* Expression Evaluation
* Basic Query Processing
* In-Memory Database Execution

This project provides a simplified view of how real database systems process and execute SQL queries internally.
