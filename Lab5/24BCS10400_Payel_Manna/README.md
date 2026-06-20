# Lab 5: Dijkstra's Shunting-Yard Expression Evaluator + Minimal SQL SELECT Parser

**Name:** Payel Manna
**Roll Number:** 24BCS10400

---

# Objective

The objective of this lab is to understand how database systems process and evaluate SQL queries internally.

This lab consists of two major components:

1. **Dijkstra's Shunting-Yard Algorithm**

   * Convert infix expressions into Reverse Polish Notation (RPN).
   * Evaluate arithmetic and boolean expressions efficiently.

2. **Minimal SQL SELECT Parser**

   * Parse SQL SELECT statements.
   * Execute queries on an in-memory dataset represented as `vector<Row>`.
   * Support filtering, projection, sorting, and limiting of results.

The implementation demonstrates how a query moves through the different stages of a database execution pipeline.

---

# Background

A typical SQL query such as:

```sql
SELECT id, name, gpa
FROM students
WHERE gpa > 3.0
ORDER BY gpa DESC
LIMIT 3;
```

cannot be executed directly.

A database first:

1. Tokenizes the query.
2. Parses the query into an internal representation.
3. Converts expressions into a format that is easy to evaluate.
4. Applies filtering conditions.
5. Projects required columns.
6. Sorts the results.
7. Returns the final output.

This lab implements a simplified version of this workflow.

---

# Part 1: Shunting-Yard Algorithm

## What is Shunting-Yard?

The Shunting-Yard Algorithm was developed by **Edsger Dijkstra** to convert an infix expression into postfix notation (Reverse Polish Notation).

### Infix Expression

```text
age * 2 + salary / 1000 > 100
```

### Postfix Expression

```text
age 2 * salary 1000 / + 100 >
```

Postfix notation removes the need for parentheses and operator precedence handling during evaluation.

---

## Why is it useful?

SQL WHERE clauses contain complex expressions such as:

```sql
WHERE age >= 22 && age <= 26
```

Evaluating infix expressions repeatedly is expensive.

The Shunting-Yard algorithm converts expressions into postfix form once, after which evaluation becomes straightforward using a stack.

---

## Supported Operators

| Operator | Meaning            | Precedence |
| -------- | ------------------ | ---------- |
| ||       | Logical OR         | 1          |
| &&       | Logical AND        | 2          |
| =        | Equality           | 3          |
| !=       | Not Equal          | 3          |
| <        | Less Than          | 4          |
| >        | Greater Than       | 4          |
| <=       | Less Than Equal    | 4          |
| >=       | Greater Than Equal | 4          |
| +        | Addition           | 5          |
| -        | Subtraction        | 5          |
| *        | Multiplication     | 6          |
| /        | Division           | 6          |
| ^        | Exponentiation     | 7          |

---

## Steps of the Algorithm

### 1. Tokenization

Expression:

```text
age * 2 + salary / 1000 > 100
```

Tokens:

```text
age
*
2
+
salary
/
1000
>
100
```

---

### 2. Infix → Postfix Conversion

Using:

* Output Queue
* Operator Stack

Result:

```text
age 2 * salary 1000 / + 100 >
```

---

### 3. RPN Evaluation

A stack is used to evaluate the postfix expression.

Example:

```text
30 2 * 50000 1000 / + 100 >
```

becomes:

```text
60 + 50 > 100
```

which evaluates to:

```text
true
```

---

# Part 2: Minimal SQL SELECT Parser

## Row Representation

Each row is represented using:

```cpp
using Value = std::variant<double, std::string>;

struct Row {
    std::unordered_map<std::string, Value> cols;
};
```

Example:

```cpp
{
    {"id",1.0},
    {"name","Alice"},
    {"age",22.0},
    {"gpa",3.8}
}
```

---

## Query Structure

The parsed query is stored in:

```cpp
struct SelectQuery
```

### Fields

| Field     | Description      |
| --------- | ---------------- |
| columns   | Selected columns |
| from      | Table name       |
| where_raw | WHERE clause     |
| order_by  | ORDER BY column  |
| order_asc | ASC / DESC       |
| limit     | Maximum rows     |

---

# SQL Features Implemented

## SELECT

Example:

```sql
SELECT id, name, gpa
FROM students
```

Returns only the requested columns.

---

## SELECT *

Example:

```sql
SELECT *
FROM students
```

Returns all columns.

---

## WHERE

Example:

```sql
WHERE gpa > 3.0
```

Filters rows based on the Shunting-Yard expression evaluator.

---

## ORDER BY

Example:

```sql
ORDER BY gpa DESC
```

Sorts rows by GPA in descending order.

---

## LIMIT

Example:

```sql
LIMIT 3
```

Restricts output to the first three rows.

---

# Query Execution Pipeline

The query execution follows:

```text
SQL Query
    |
    v
Tokenizer
    |
    v
Parser
    |
    v
SelectQuery Structure
    |
    v
WHERE Evaluation
(Shunting-Yard + RPN)
    |
    v
Projection
(SELECT columns)
    |
    v
ORDER BY
    |
    v
LIMIT
    |
    v
Final Result Set
```

---

# Sample Dataset

The lab uses an in-memory student table:

| ID | Name  | Age | GPA |
| -- | ----- | --- | --- |
| 1  | Alice | 22  | 3.8 |
| 2  | Bob   | 25  | 2.9 |
| 3  | Carol | 21  | 3.5 |
| 4  | Dave  | 30  | 3.1 |

---

# Sample Queries

## Query 1

```sql
SELECT id, name, gpa
FROM students
WHERE gpa > 3.0
ORDER BY gpa DESC
LIMIT 3
```

### Output

```text
gpa=3.8  name=Alice  id=1
gpa=3.5  name=Carol  id=3
gpa=3.1  name=Dave   id=4
```

---

## Query 2

```sql
SELECT *
FROM students
WHERE age >= 22 && age <= 26
```

### Output

```text
gpa=3.8  name=Alice  age=22  id=1
gpa=2.9  name=Bob    age=25  id=2
```

---

# Time Complexity

## Tokenization

```text
O(n)
```

where `n` is the length of the expression.

---

## Shunting-Yard Conversion

```text
O(n)
```

Each token is pushed and popped at most once.

---

## RPN Evaluation

```text
O(n)
```

Single-pass evaluation using a stack.

---

## Query Execution

### Filtering

```text
O(rows × expression_size)
```

### Sorting

```text
O(rows log rows)
```

### Projection

```text
O(rows × selected_columns)
```

---

# Build Instructions

Compile:

```bash
g++ -std=c++17 main.cpp -o lab5
```

Run:

```bash
./lab5
```

---

# Learning Outcomes

Through this lab, the following concepts were explored:

* Dijkstra's Shunting-Yard Algorithm
* Reverse Polish Notation (RPN)
* Stack-Based Expression Evaluation
* SQL Parsing
* Query Execution Pipelines
* Projection and Filtering
* Sorting and Limiting Result Sets
* Variant-Based Data Storage using C++17
* Database Query Processing Internals

---

# Conclusion

This lab demonstrates how a database engine transforms a SQL query into executable operations.

The Shunting-Yard algorithm efficiently handles expression parsing and evaluation, while the SQL parser and executor simulate the fundamental stages of a relational database query processor:

```text
Parse → Filter → Project → Sort → Limit → Return Result
```

The implementation provides a simplified but practical view of how modern database systems process SQL queries internally.
