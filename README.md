# SQL Query Parser + Shunting Yard Demo

A small C++17 project that demonstrates two ideas together:

1. A simple SQL-like `SELECT` query parser and executor.
2. A shunting-yard based expression tokenizer, converter, and evaluator for `WHERE` conditions.

The project works on an in-memory table of student records and supports basic filtering, sorting, and limiting.

## Features

- Parse basic SQL-style queries
- Support `SELECT *` or selected columns
- Support `WHERE` expressions with:
  - arithmetic operators: `+`, `-`, `*`, `/`, `^`
  - comparison operators: `=`, `!=`, `<`, `>`, `<=`, `>=`
  - logical operators: `AND`, `OR`, `&&`, `||`
  - parentheses
- Support `ORDER BY` with `ASC` / `DESC`
- Support `LIMIT`
- Evaluate expressions using the shunting-yard algorithm

## Project Structure

```text
.
├── query_parser.cpp      # SQL-like parser, executor, and demo main()
├── shunting_yard.cpp     # Expression tokenizer, RPN conversion, evaluator
└── README.md
```

## How It Works

### 1. Query Parsing

`query_parser.cpp` splits the SQL string into words and extracts:

- selected columns
- table name
- `WHERE` expression
- `ORDER BY` column and direction
- `LIMIT` value

### 2. Expression Handling

`shunting_yard.cpp` converts the `WHERE` condition into Reverse Polish Notation (RPN) using the shunting-yard algorithm.

Then it evaluates the RPN expression using variable values from each row.

### 3. Row Execution

The query is run against an in-memory vector of rows. Each row stores columns using:

- `double` for numeric values
- `string` for text values

## Example Queries

The demo program includes these example queries:

```sql
SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3
SELECT * FROM students WHERE age >= 22 AND age <= 26
```

## Build Instructions

Use any C++17-compatible compiler.

### g++

```bash
g++ -std=c++17 query_parser.cpp shunting_yard.cpp -o query_parser
```

### clang++

```bash
clang++ -std=c++17 query_parser.cpp shunting_yard.cpp -o query_parser
```

## Run

```bash
./query_parser
```

## Output

The program first shows the shunting-yard demo, then prints the result of each SQL query.

## Notes

- This is not a full SQL engine.
- The parser is intentionally simple and only supports the syntax used in the assignment.
- Table name parsing is present, but the data is currently hardcoded in memory.
- Column ordering in output may vary because `unordered_map` does not preserve order.
