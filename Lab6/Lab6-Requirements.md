# Lab Session 6: Shunting-Yard Algorithm + Minimal SQL SELECT Parser over `vector<Row>`

## Objective

### Part 1 – Expression Evaluation Engine

Implement Dijkstra's Shunting-Yard Algorithm to:

* Tokenize infix expressions.
* Convert infix expressions to postfix notation (Reverse Polish Notation).
* Evaluate arithmetic and boolean expressions.
* Support variables through a symbol table.
* Demonstrate expression evaluation using sample inputs.

### Part 2 – Minimal SQL Execution Engine

Build a lightweight SQL parser and execution engine capable of executing SELECT queries on an in-memory dataset represented using `vector<Row>`.

---

# Learning Outcomes

After completing this lab, students should be able to:

* Understand operator precedence and associativity.
* Implement the Shunting-Yard Algorithm.
* Evaluate postfix expressions using stacks.
* Parse SQL SELECT statements.
* Execute SQL-style filtering and projection on in-memory data.
* Understand how query execution works inside a database engine.

---

# Part 1: Shunting-Yard Algorithm

## Supported Operators

### Arithmetic Operators

| Operator | Meaning        |
| -------- | -------------- |
| +        | Addition       |
| -        | Subtraction    |
| *        | Multiplication |
| /        | Division       |
| ^        | Exponentiation |

### Comparison Operators

| Operator | Meaning               |
| -------- | --------------------- |
| <        | Less Than             |
| >        | Greater Than          |
| <=       | Less Than or Equal    |
| >=       | Greater Than or Equal |
| ==       | Equality              |
| !=       | Not Equal             |

### Logical Operators

| Operator | Meaning     |
| -------- | ----------- |
| AND      | Logical AND |
| OR       | Logical OR  |

### Parentheses

Support nested parentheses for grouping.

---

## Expression Engine Requirements

Implement:

1. Tokenization
2. Infix → Postfix Conversion
3. Postfix Evaluation
4. Variable Resolution

### Example Expression

```text
(age * 2 + salary / 1000) > 100
```

### Expected Workflow

```text
Expression
    ↓
Tokenization
    ↓
Shunting-Yard Algorithm
    ↓
Postfix Expression
    ↓
Stack-Based Evaluation
    ↓
Final Result
```

---

# Part 2: Minimal SQL SELECT Engine

## Supported SQL Statements

### Query 1

```sql
SELECT *
FROM students
```

### Query 2

```sql
SELECT id, name
FROM students
WHERE age > 20
```

### Query 3

```sql
SELECT id, name, gpa
FROM students
WHERE gpa >= 3.5
ORDER BY gpa DESC
```

### Query 4

```sql
SELECT *
FROM students
WHERE age >= 20
LIMIT 3
```

---

# Supported SQL Features

## SELECT *

Return all columns.

## Column Projection

Return only requested columns.

Example:

```sql
SELECT id, name
FROM students
```

## WHERE Clause

Filter rows using expressions evaluated by the Shunting-Yard Engine.

Example:

```sql
WHERE age > 20
```

## ORDER BY

Support:

```sql
ORDER BY age ASC
ORDER BY gpa DESC
```

## LIMIT

Restrict the number of rows returned.

Example:

```sql
LIMIT 5
```

---

# Data Layer

Create:

```cpp
struct Record;
struct Table;
```

Use STL containers to store rows.

---

## Student Schema

| Column | Type    |
| ------ | ------- |
| id     | Integer |
| name   | String  |
| age    | Integer |
| gpa    | Double  |

Create at least 8–10 sample records.

---

# Query Execution Pipeline

The execution pipeline should be:

```text
SQL Query
    ↓
Tokenization
    ↓
Parsing
    ↓
Query Object
    ↓
WHERE Evaluation (Shunting-Yard)
    ↓
Projection
    ↓
Sorting
    ↓
Limit
    ↓
Output
```

---

# Additional Requirements

Implement the following features:

## Pretty Table Output

Display query results in a readable tabular format.

## Query Statistics

Display:

* Rows scanned
* Rows returned
* Query execution summary

## Error Handling

Handle:

### Unknown Columns

Example:

```sql
SELECT salary
FROM students
```

### Invalid Operators

Example:

```sql
WHERE age @@ 20
```

### Syntax Errors

Example:

```sql
SELECT FROM students
```

### Empty Results

Gracefully handle queries that return no rows.

---

# Suggested Class Design

```cpp
class Token;
class Lexer;
class ExpressionParser;
class ExpressionEvaluator;

class SQLLexer;
class SQLParser;
class QueryExecutor;
```

---

# Expected Project Structure

```text
Lab6/
│
├── Lab6-Requirements.md
├── README.md
├── sql_parser.cpp
└── Makefile
```

---

# Compilation

```bash
g++ -std=c++17 sql_parser.cpp -o sql_parser
```

---

# Execution

```bash
./sql_parser
```

---

# Deliverables

1. Source Code (`sql_parser.cpp`)
2. Documentation (`README.md`)
3. Requirements File (`Lab6-Requirements.md`)
4. Build Script (`Makefile`)

---

# Evaluation Criteria

| Component                    | Weight |
| ---------------------------- | ------ |
| Shunting-Yard Implementation | 30%    |
| Expression Evaluation        | 20%    |
| SQL Parsing                  | 20%    |
| Query Execution              | 20%    |
| Code Quality & Documentation | 10%    |

---

# Submission Notes

* Use modern C++17.
* Follow modular design principles.
* Add meaningful comments.
* Ensure code compiles on Linux using g++.
* Demonstrate multiple sample SQL queries.
