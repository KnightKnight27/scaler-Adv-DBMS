# Lab 7: Dijkstra's Shunting-Yard Expression Evaluator & SQL SELECT Parser

**Student:** Lokendra Singh Rajawat — 23bcs10075  
**Subject:** Advanced Database Management Systems

---

## Overview

This lab implements a query execution pipeline in C++ comprising:
1.  **Dijkstra's Shunting-Yard Algorithm**: A compiler that converts infix conditional expressions (such as `age >= 21 AND course = 'CS'`) into Postfix (Reverse Polish Notation, RPN).
2.  **Stack-based Expression Evaluator**: An interpreter that evaluates postfix token queues against individual data records (`Row` instances) with dynamic type resolutions.
3.  **Minimal SQL SELECT Parser**: A syntax scanner that extracts column projections, target tables, and WHERE condition strings from SQL queries.
4.  **In-Memory Relational Engine**: A lightweight engine that executes queries over a `std::vector<Row>` using the parsed expression filters and projects results in a formatted ASCII table.

---

## System Architecture & Data Flow

The query execution flow follows a clean sequential pipeline:

```
[SQL Query String] 
       │
       ▼
 [SQL Parser] ──(Projections & Table Name)──► [Database Engine]
       │                                              ▲
       ▼ (Condition Tokens)                           │
 [Shunting-Yard Compiler] ──(RPN Token Queue)──► [Row Evaluator]
                                                      │ (Filter Result)
                                                      ▼
                                              [Projected Output Table]
```

1.  **Lexical Analysis (SQL Tokenizer)**: Splitting the SQL string into individual strings while preserving spaces within single-quoted literals (e.g. `'Computer Science'`).
2.  **Syntactic Analysis (SQL Parser)**: Scans the tokens, validates keywords (`SELECT`, `FROM`, `WHERE`), extracts the list of projection columns, and isolates the where-clause infix expression.
3.  **Expression Parsing (Shunting-Yard)**: Compiles the infix condition tokens into a postfix RPN token queue using operator precedence rules.
4.  **Execution & Projection**: The database engine fetches the target table, loops through its rows, passes each row to the RPN Evaluator, and—if the evaluator returns true—projects the requested columns and renders the output to console.

---

## Operator Precedence Rules

To correctly evaluate complex expressions without parentheses ambiguities, the Shunting-Yard parser implements standard SQL operator precedence:

| Precedence | Operator | Associativity | Description |
| :--- | :--- | :--- | :--- |
| **5** (Highest) | `*`, `/` | Left-to-right | Multiplication & Division |
| **4** | `+`, `-` | Left-to-right | Addition & Subtraction |
| **3** | `=`, `!=`, `<>`, `>`, `<`, `>=`, `<=` | Left-to-right | Relational comparison |
| **2** | `NOT` | Right-to-left | Logical negation |
| **1** | `AND` | Left-to-right | Logical conjunction |
| **0** (Lowest) | `OR` | Left-to-right | Logical disjunction |

*Note: By keeping `NOT` lower than comparison operators (e.g., precedence 2 vs. 3), expressions like `NOT course = 'CS'` bind correctly as `NOT (course = 'CS')` rather than `(NOT course) = 'CS'`.*

---

## Compilation & Run Instructions

To compile and run the engine:

```bash
# Navigate to lab7 directory
cd lab7

# Compile the source files
make

# Run the automated query tests
./select_parser test

# Run in interactive SQL shell (REPL) mode
make run
```

---

## Sample Execution Output

Here is the output from the automated test harness:

```text
==========================================================
           RUNNING AUTOMATED REPRESENTATIVE TESTS           
==========================================================

SQL> SELECT * FROM students;
+------------+------------+-----------+-----+------+--------+
| student_id | first_name | last_name | age | gpa  | course |
+------------+------------+-----------+-----+------+--------+
| 1          | Alice      | Smith     | 20  | 3.85 | CS     |
| 2          | Bob        | Jones     | 22  | 3.40 | EE     |
| 3          | Charlie    | Brown     | 19  | 3.92 | CS     |
| 4          | David      | Wilson    | 21  | 2.95 | ME     |
| 5          | Eva        | Davis     | 20  | 3.70 | EE     |
| 6          | Frank      | Miller    | 23  | 3.15 | CS     |
+------------+------------+-----------+-----+------+--------+
6 row(s) in set.

SQL> SELECT student_id, first_name, gpa FROM students WHERE gpa > 3.50;
+------------+------------+------+
| student_id | first_name | gpa  |
+------------+------------+------+
| 1          | Alice      | 3.85 |
| 3          | Charlie    | 3.92 |
| 5          | Eva        | 3.70 |
+------------+------------+------+
3 row(s) in set.

SQL> SELECT first_name, last_name, age, course FROM students WHERE age >= 21 AND course = 'CS';
+------------+-----------+-----+--------+
| first_name | last_name | age | course |
+------------+-----------+-----+--------+
| Frank      | Miller    | 23  | CS     |
+------------+-----------+-----+--------+
1 row(s) in set.

SQL> SELECT first_name, course, gpa FROM students WHERE (course = 'CS' OR course = 'EE') AND gpa >= 3.70;
+------------+--------+------+
| first_name | course | gpa  |
+------------+--------+------+
| Alice      | CS     | 3.85 |
| Charlie    | CS     | 3.92 |
| Eva        | EE     | 3.70 |
+------------+--------+------+
3 row(s) in set.

SQL> SELECT first_name, course FROM students WHERE NOT course = 'CS';
+------------+--------+
| first_name | course |
+------------+--------+
| Bob        | EE     |
| David      | ME     |
| Eva        | EE     |
+------------+--------+
3 row(s) in set.

SQL> SELECT first_name, age FROM students WHERE age * 2 > 40;
+------------+-----+
| first_name | age |
+------------+-----+
| Bob        | 22  |
| David      | 21  |
| Frank      | 23  |
+------------+-----+
3 row(s) in set.
```

---

## Key Design Insights & Implementation Trade-Offs

1.  **Dynamic Type Casts in Evaluation**: Databases store values as strings (VARCHAR) on disk, but queries often compare them as numbers or booleans. In `evaluateRPN()`, the evaluator dynamically checks if the operands represent numbers (using a strict decimal validation helper). If either side is numeric, both values are compared as doubles. Otherwise, they are compared lexicographically as strings.
2.  **No Ast Overhead**: Rather than generating a full Abstract Syntax Tree (AST) which consumes recursive stack overhead, this design compiles infix tokens straight to a sequential Postfix (RPN) array. Evaluating RPN in C++ requires a single loop and a local stack, maximizing cache locality and query throughput.
3.  **Strict Error Handling**: The parser captures projections of non-existent fields or table queries and throws clean, localized syntax and schema mismatch error messages instead of causing undefined behavior or segfaults.
