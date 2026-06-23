# Lab 7: Shunting-Yard Algorithm & Minimal SQL SELECT Parser

This directory contains the implementation of **Lab 7**, which focuses on Dijkstra's Shunting-Yard algorithm for evaluating infix arithmetic/boolean expressions and a minimal SQL parser that handles `SELECT` queries over an in-memory dataset of `vector<Row>`.

## Objective
1. Implement **Dijkstra's Shunting-Yard algorithm** to evaluate infix expressions (such as those found in SQL `WHERE` clauses).
2. Build a **minimal SQL parser** that handles `SELECT` queries and executes them against an already-fetched `vector<Row>`.

## Files
- `sql_parser.cpp`: Contains the complete implementation of the shunting-yard algorithm, tokenization, abstract syntax tree building, and a minimal execution engine to simulate fetching and filtering rows.

## Features
- **Infix to Postfix Conversion**: Uses the shunting-yard algorithm to evaluate logical and arithmetic operations while respecting operator precedence.
- **SQL Parser**: Processes a minimal syntax for `SELECT`, `FROM`, `WHERE`, `ORDER BY`, and `LIMIT` clauses.
- **Query Execution Engine**: Applies projection and sorting according to the user query over pre-fetched memory data structures (`vector<Row>`).

## How to Compile & Run
To run the project, ensure you have a modern C++ compiler (supporting C++17 or later). You can use GCC/G++ or Clang:

```bash
g++ -std=c++17 -o sql_parser sql_parser.cpp
./sql_parser
```

### Expected Output
The program includes two parts:
1. **Shunting Demo**: Evaluates the expression `age * 2 + salary / 1000 > 100` and displays its Reverse Polish Notation (RPN) and the final boolean result based on hardcoded variables.
2. **Query Demo**: Demonstrates parsing and evaluating actual minimal SQL `SELECT` statements over an in-memory `students` dataset.
