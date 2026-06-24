# Lab Session 6: Shunting-Yard Algorithm & Minimal SQL SELECT Parser

This repository contains a C++ implementation of the Shunting-Yard algorithm combined with a minimal SQL `SELECT` parser. It demonstrates how a database parses query strings and evaluates expressions (like `WHERE` clauses) on in-memory row data.

## Overview

The code consists of two main parts:

### Part 1: Shunting-Yard Algorithm
Converts infix expressions (e.g., `age > 20 && salary < 50000`) into Reverse Polish Notation (RPN). RPN allows us to evaluate logical and arithmetic expressions efficiently using a simple stack, without needing complex Abstract Syntax Trees (AST) or recursion.

### Part 2: Minimal SQL SELECT Parser
A basic tokenizer and parser that processes SQL `SELECT` queries over an in-memory `std::vector<Row>`. It supports:
- **Projection**: `SELECT col1, col2` or `SELECT *`
- **Filtering**: `WHERE` clauses, evaluated per-row using the Shunting-Yard algorithm
- **Sorting**: `ORDER BY col [ASC|DESC]`
- **Limiting**: `LIMIT N`

## Prerequisites

- A C++17 compatible compiler (e.g., GCC, Clang).

## Compilation & Execution

To compile and run the project, execute the following command in your terminal:

```bash
g++ -std=c++17 -o sql_parser sql_parser.cpp
./sql_parser
```

## How It Works

1. **Tokenization:** Breaks the SQL string and the `WHERE` expression into meaningful tokens.
2. **Parsing:** The `parse_select()` function interprets the SQL keywords to build a `SelectQuery` struct containing the requested columns, filter logic, sort conditions, and limit.
3. **Execution:** The `execute()` function iterates through the `std::vector<Row>`, mapping column values to variables. It filters the rows using the RPN evaluation of the `WHERE` clause, sorts the resulting subset, and limits the final output.

## Code Structure
- **`sql_parser.cpp`**: Contains the full implementation.
  - `tokenize()` & `to_rpn()`: Handles tokenizing strings and converting infix to RPN.
  - `eval_rpn()`: Evaluates the parsed expression.
  - `Row` & `SelectQuery`: Data structures representing the schema and parsed AST.
  - `parse_select()`: Converts raw SQL strings to the `SelectQuery` AST.
  - `execute()`: Runs the query against a `vector<Row>` dataset.
  - `main()`: A demo with hardcoded data and example queries.

## Example Output

```
Expression : age * 2 + salary / 1000 > 100
RPN        : age 2 * salary 1000 / + 100 > 
Result     : true

SQL: SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3
id=1  name=Alice  gpa=3.8  
id=3  name=Carol  gpa=3.5  
id=4  name=Dave  gpa=3.1  

SQL: SELECT * FROM students WHERE age >= 22 && age <= 26
id=1  name=Alice  age=22  gpa=3.8  
id=2  name=Bob  age=25  gpa=2.9  
```
