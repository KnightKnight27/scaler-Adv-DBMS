# ADBMS Lab 6 – Shunting-Yard Based SQL Query Engine (C++17)

**Name:** Patel Jash
**Roll Number:** 24bcs10632
**Course:** Advanced Database Management Systems (ADBMS)
**Institution:** Scaler School of Technology

---

## Overview

This project builds a miniature SQL query execution engine using C++17, driven by Dijkstra's Shunting-Yard Algorithm. The core aim of this lab is to illustrate the internal mechanics of relational databases when parsing, processing, and running SQL SELECT statements.

The system is composed of two primary parts:

1. **Shunting-Yard Implementation**
    * Translates infix boolean expressions (found in SQL WHERE clauses) into postfix notation (Reverse Polish Notation or RPN).
    * Resolves operator precedence and grouped parentheses seamlessly.

2. **SQL Execution Engine**
    * Breaks down SQL text into tokens.
    * Parses SELECT syntax structures.
    * Performs row filtering, column projection, sorting, and row limits on a mock in-memory data table.

The WHERE condition is translated into postfix just once at query compile time, allowing for rapid row-by-row evaluation during execution.

---

## Directory Layout

```text
Lab-6/
├── main.cpp
├── sql_engine.cpp
├── sql_engine.h
├── Makefile
└── README.md
```

### File Breakdown

| File           | Purpose                                   |
| -------------- | ----------------------------------------- |
| main.cpp       | The main driver and validation test cases |
| sql_engine.h   | Declarations for data types and functions |
| sql_engine.cpp | Core logic for the SQL execution engine   |
| Makefile       | Automated build instructions              |
| README.md      | Project documentation and guides          |

---

## Supported Functionality

### Query Operations

Recognized query syntax:

```sql
SELECT column_names
FROM table_name
[WHERE logic_condition]
[ORDER BY column_name ASC|DESC]
[LIMIT count]
```

### Valid Operators

```sql
=
!=
<
<=
>
>=
AND
OR
NOT
```

### Supported Data Types

* Integer Values (`long long`)
* String Values (`std::string`)

---

## Processing Architecture

```text
SQL Statement
    │
    ▼
Lexical Scanner (Tokenizer)
    │
    ▼
Syntax Parser
    │
    ▼
Shunting-Yard Converter
(Infix → Postfix)
    │
    ▼
Execution Module
    │
    ├── Row Filtering (WHERE)
    ├── Column Projection
    ├── Sorting (ORDER BY)
    └── Limiting (LIMIT)
    │
    ▼
Final Output Table
```

---

## Shunting-Yard Details

Dijkstra's algorithm converts standard infix mathematical/logical expressions into postfix, making them trivial to evaluate using a basic stack.

Example Transformation:

```text
Infix:
age > 25 AND (dept = 'Sales' OR salary >= 100000)

Postfix:
age 25 > dept 'Sales' = salary 100000 >= OR AND
```

### Operator Hierarchy

| Priority Level | Recognized Operators |
| -------------- | -------------------- |
| 4 (Highest)    | =, !=, <, <=, >, >=  |
| 3              | NOT                  |
| 2              | AND                  |
| 1 (Lowest)     | OR                   |

---

## Query Evaluation

The SQL engine runs statements following this logical sequence:

1. Filter rows matching the WHERE criteria.
2. Project only the selected columns.
3. Sort the resulting rows based on ORDER BY.
4. Truncate the results based on the LIMIT clause.

This mirrors the operational pipeline of production-grade SQL databases.

---

## Sample Usage

```sql
SELECT name, dept, salary
FROM employees
WHERE age > 25
AND (dept = 'Sales' OR salary >= 100000)
ORDER BY salary DESC;
```

---

## Complexity Estimates

| Step                     | Time Complexity |
| ------------------------ | --------------- |
| Tokenization             | O(L)            |
| Parsing                  | O(T)            |
| Infix to Postfix         | O(T)            |
| Condition Evaluation     | O(T) per row    |
| Complete Filtering       | O(N × T)        |
| Result Sorting           | O(N log N)      |

*(L = SQL query string length, T = Total tokens, N = Table row count)*

---

## How to Build and Run

To compile and execute immediately:

```bash
make run
```

Or step-by-step compilation:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic main.cpp sql_engine.cpp -o adbms_lab6
./adbms_lab6
```

---

## Final Thoughts

This lab highlights the fundamental operations required to evaluate SQL queries. Building this engine from the ground up—covering lexical analysis, parsing, infix-to-postfix conversion, and final execution—offers an insightful look into the mechanics of professional database systems.
