# Lab 5 - Shunting Yard Expression Evaluator and Minimal SQL Parser

**Name:** Abhijit P
**Roll No:** 24bcs10175

## Objective

The objective of this lab was to implement:

1. Dijkstra's Shunting Yard Algorithm for converting infix expressions into postfix expressions and evaluating them.
2. A minimal SQL SELECT parser operating on a collection of rows stored in a C++ vector.

The lab demonstrates fundamental concepts used in query processing, expression evaluation, parsing, and database systems.

---

## Part 1 - Shunting Yard Expression Evaluator

### Overview

The Shunting Yard Algorithm was developed by Edsger Dijkstra to convert infix mathematical expressions into postfix notation.

Postfix notation is easier to evaluate because operator precedence and parentheses are already resolved.

Example:

### Infix Expression

```text
(3 + 4) * 2
```

### Postfix Expression

```text
3 4 + 2 *
```

### Result

```text
14
```

---

## Algorithm Workflow

### Step 1 - Read Expression

The input expression is scanned character by character.

Example:

```text
(3+4)*2
```

### Step 2 - Convert to Postfix

The algorithm uses a stack to manage operators.

Rules:

* Operands are added directly to the output.
* Operators are pushed onto the stack.
* Higher precedence operators are processed first.
* Parentheses control evaluation order.

### Step 3 - Evaluate Postfix

A second stack is used.

Rules:

* Numbers are pushed onto the stack.
* When an operator is encountered:

  * Pop two operands.
  * Apply the operation.
  * Push the result back.

Final result remains at the top of the stack.

---

## Supported Operators

| Operator | Operation      |
| -------- | -------------- |
| +        | Addition       |
| -        | Subtraction    |
| *        | Multiplication |
| /        | Division       |

---

## Time Complexity

| Operation                   | Complexity |
| --------------------------- | ---------- |
| Infix to Postfix Conversion | O(n)       |
| Postfix Evaluation          | O(n)       |

---

## Part 2 - Minimal SQL SELECT Parser

### Overview

The second part of the lab implements a simple SQL parser operating on a vector of rows.

Instead of using a real database engine, records are stored in memory using:

```cpp
std::vector<Row>
```

Each row contains:

```cpp
struct Row {
    int id;
    std::string name;
    int age;
};
```

---

## Sample Data

```text
1 Alice 20
2 Bob 25
3 Charlie 18
4 David 22
```

---

## Supported SQL Syntax

### Select All Columns

```sql
SELECT * FROM students
```

### Select Specific Column

```sql
SELECT name FROM students
```

### Select with WHERE Clause

```sql
SELECT name FROM students WHERE age > 20
```

### Filtering by ID

```sql
SELECT id FROM students WHERE age < 21
```

---

## Query Processing Steps

### Tokenization

The query string is divided into tokens.

Example:

```sql
SELECT name FROM students WHERE age > 20
```

Tokens:

```text
SELECT
name
FROM
students
WHERE
age
>
20
```

### Parsing

The parser identifies:

* Selected column
* Table name
* WHERE clause
* Comparison operator
* Filter value

### Execution

The vector is scanned row by row.

Matching rows are returned based on the WHERE condition.

---

## Supported Comparison Operators

| Operator | Meaning      |
| -------- | ------------ |
| >        | Greater Than |
| <        | Less Than    |
| =        | Equal To     |

---

## Sample Queries

### Query 1

```sql
SELECT * FROM students
```

Output:

```text
1 Alice 20
2 Bob 25
3 Charlie 18
4 David 22
```

### Query 2

```sql
SELECT name FROM students WHERE age > 20
```

Output:

```text
Bob
David
```

### Query 3

```sql
SELECT id FROM students WHERE age < 21
```

Output:

```text
1
3
```

---

## Relation to Database Systems

### Expression Evaluation

Database systems frequently evaluate expressions in:

* Query predicates
* Computed columns
* Aggregations
* Query optimization

The Shunting Yard Algorithm demonstrates how expressions can be parsed and evaluated efficiently.

### SQL Parsing

Every database system contains a parser that converts SQL text into an internal representation before execution.

This lab demonstrates a simplified version of that process using:

* Tokenization
* Parsing
* Filtering
* Result generation

---

## Project Structure

```text
Lab5/
│
├── README.md
│
├── shunting_yard/
│   └── expression_evaluator.cpp
│
└── sql_parser/
    └── sql_parser.cpp
```

---

## Build Instructions

### Expression Evaluator

```bash
g++ expression_evaluator.cpp -o evaluator
./evaluator
```

### SQL Parser

```bash
g++ sql_parser.cpp -o parser
./parser
```

---

## Complexity Analysis

### Shunting Yard Evaluator

| Operation  | Complexity |
| ---------- | ---------- |
| Conversion | O(n)       |
| Evaluation | O(n)       |

### SQL Parser

| Operation | Complexity |
| --------- | ---------- |
| Parsing   | O(n)       |
| Row Scan  | O(m)       |

Where:

* n = query length
* m = number of rows

---

## Conclusion

This lab demonstrated two important concepts used in database systems and programming language processing.

The Shunting Yard Algorithm converts infix expressions into postfix notation and evaluates them efficiently using stacks. The SQL parser demonstrates how database systems process SQL statements, extract query components, apply filtering conditions, and return matching results.

Together, these implementations provide insight into expression parsing, query processing, and execution mechanisms used in real database systems.
