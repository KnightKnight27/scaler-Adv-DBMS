# Lab 5 - Dijkstra's Shunting-Yard Evaluator and SQL SELECT Parser

**Name:** Shifa
**Roll Number:** 24BCS10354

## Overview

This lab demonstrates two fundamental concepts used in database systems and query processing:

1. **Dijkstra's Shunting-Yard Algorithm**

   * Converts infix expressions into postfix (Reverse Polish Notation).
   * Evaluates logical and comparison expressions efficiently.

2. **SQL SELECT Query Parsing**

   * Parses simple SQL-like queries.
   * Extracts SELECT, FROM, and WHERE clauses.
   * Evaluates WHERE conditions against records stored in memory.

The implementation simulates a lightweight query engine operating on a collection of employee records stored in a `std::vector<Employee>`.

---

## Objectives

* Understand tokenization of SQL-like expressions.
* Implement operator precedence handling using the Shunting-Yard algorithm.
* Convert infix expressions to postfix notation.
* Evaluate postfix expressions against row data.
* Parse simple SQL SELECT statements.
* Filter records based on WHERE clause conditions.

---

## Project Structure

```text
Lab7/
│
├── Makefile
│
├── dijkstraShunting/
│   └── main.cpp
│
└── queryParsing/
    └── main.cpp
```

---

## Features

### Shunting-Yard Evaluator

Supports:

* Parentheses `(` `)`
* Comparison operators:

  * `>`
  * `<`
  * `>=`
  * `<=`
  * `=`
  * `!=`
* Logical operators:

  * `AND`
  * `OR`
* Numeric literals
* Column references (`id`, `age`)

Example:

```sql
id > 3 AND (age < 25 OR age >= 30)
```

---

### SQL SELECT Parser

Supports queries of the form:

```sql
SELECT column_name
FROM employees
WHERE condition
```

Examples:

```sql
SELECT name FROM employees WHERE id >= 3 OR age < 20
```

```sql
SELECT name FROM employees WHERE id > 3 AND age >= 30
```

```sql
SELECT id FROM employees WHERE (age < 25 AND id != 2) OR age >= 30
```

---

## Sample Employee Dataset

```cpp
{
    {"Rama Krishnan", 1, 19},
    {"Aarav", 2, 20},
    {"Karan", 3, 19},
    {"Sneha", 4, 21},
    {"Vivaan", 5, 20},
    {"Ishaan", 6, 31},
    {"Meera", 7, 22},
    {"Devansh", 8, 33}
}
```

---

## Algorithm

### Step 1: Tokenization

The input query is split into individual tokens such as:

```text
id > 3 AND age < 25
```

becomes

```text
[id] [>] [3] [AND] [age] [<] [25]
```

---

### Step 2: Infix to Postfix Conversion

The Shunting-Yard algorithm converts:

```text
id > 3 AND age < 25
```

to

```text
id 3 > age 25 < AND
```

using operator precedence rules.

---

### Step 3: Postfix Evaluation

For each employee record:

1. Replace column names with actual values.
2. Evaluate comparison operations.
3. Evaluate logical operations.
4. Produce a final boolean result.

Only matching rows are returned.

---

## Compilation

Build all programs:

```bash
make
```

---

## Running

Run both modules:

```bash
make run
```

Run only the Shunting-Yard evaluator:

```bash
make run-shunting
```

Run only the SQL parser:

```bash
make run-parser
```

---

## Cleaning

Remove generated executables:

```bash
make clean
```

---

## Concepts Demonstrated

* Tokenization
* Parsing
* Dijkstra's Shunting-Yard Algorithm
* Reverse Polish Notation (RPN)
* Operator Precedence
* SQL Query Processing
* Expression Evaluation
* In-Memory Database Filtering
* Stack-Based Computation

---

## Conclusion

This lab implements a simplified database query engine that combines SQL query parsing with Dijkstra's Shunting-Yard algorithm. The project demonstrates how database systems process WHERE clause conditions, convert expressions into postfix form, and evaluate them efficiently against stored records.
