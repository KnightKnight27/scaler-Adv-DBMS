# SQL SELECT Parser & Query Evaluator

## Included Files

| File | Description |
|------|-------------|
| `main.cpp` | Core implementation containing the Lexer, Parser, AST definition, Evaluator and execution driver. |
| `makefile` | Build script configured with standard C++17 compiler flags. |
| `readme.md` | This technical documentation. |

## Overview

This implementation demonstrates a minimal **SQL SELECT parser** that can parse and evaluate `SELECT` queries with `WHERE` clauses over a `vector<Row>` in-memory table.

## Features

- **Lexer**: Tokenizes SQL input into keywords, identifiers, operators, and punctuation.
- **Parser**: Recursively parses SELECT statements with FROM and WHERE clauses.
- **AST**: Expression nodes for columns, literals, and binary operations.
- **Evaluator**: Evaluates WHERE conditions on each row and filters results.

## Supported Operations

- Column reference: `name`
- Literal values: `30`, `"text"`
- Comparisons: `=`, `<`, `>`, `<=`, `>=`, `!=`

## Data Structure

```cpp
using Row = std::unordered_map<std::string, std::string>;
```

Rows are represented as hash maps of column names to string values.

## Build

```bash
cd queryparsing
make
```

## Run

```bash
./query_parser
```

## Example Output

```
=== Original Table ===
name=Alice age=30 salary=50000
name=Bob age=25 salary=45000
name=Charlie age=35 salary=60000
name=David age=28 salary=48000

Query: SELECT name, age FROM table WHERE age >= 30

=== Query Result ===
Columns: name age

Alice 30
Charlie 35
```

## Parser Flow

1. **Lexer** breaks input into tokens.
2. **Parser** matches grammar:
   - `SELECT col1, col2, ... FROM table WHERE expr`
3. **AST** nodes evaluate expressions against each row.
4. **Evaluator** filters and projects rows based on WHERE condition.

## Query Syntax

```
SELECT column1, column2, ...
FROM table
WHERE condition
```

**Note**: This is a minimal implementation; production SQL parsers handle much more complex syntax.

## Time Complexity

- Lexing: O(n)
- Parsing: O(m) where m = number of tokens
- Evaluation: O(r * c) where r = rows, c = conditions
