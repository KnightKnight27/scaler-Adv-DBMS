# Part 2: Minimal SQL SELECT Parser

This directory contains the implementation of a **Minimal SQL Parser** that can read basic `SELECT` queries and execute them against an in-memory `vector<Row>` dataset.

## Files
- `sql_parser.cpp`: Contains the complete SQL parsing logic, along with the required Shunting-Yard algorithm to evaluate the `WHERE` clauses.

## Features
- **SQL Parsing:** Supports extracting `SELECT` (columns), `FROM`, `WHERE`, `ORDER BY`, and `LIMIT` clauses from a raw query string.
- **Query Execution Engine:** Simulates a database execution node that iterates through rows, evaluates the `WHERE` condition via Shunting-Yard, projects selected columns, applies sorting (`ORDER BY`), and enforces limits (`LIMIT`).
- **Dynamic Typing:** Handles column values generically via `std::variant<double, std::string>`.

## How to Compile & Run
To run the parser and executor, you need a C++17 compliant compiler:

```bash
g++ -std=c++17 -o sql_parser sql_parser.cpp
./sql_parser
```

### Expected Output
The program will display the Shunting-Yard demo first, followed by executing two test SQL queries:
1. `SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3`
2. `SELECT * FROM students WHERE age >= 22 && age <= 26`

It will print out the resulting rows for each query, demonstrating filtering, projection, and sorting operations.
