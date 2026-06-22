# Lab 7: SQL Query Parser & Expression Evaluator

**Course:** Advanced DBMS (Scaler)
**Author:** Praveen Kumar 24bcs10048

Two C++ programs that demonstrate how a database engine processes SQL WHERE clauses.

## Part A -- SQL Query Parser

Lexes and parses a SQL SELECT statement, then evaluates the WHERE clause against an in-memory table.

```bash
g++ -std=c++17 -O2 -o query_parser query_parser.cpp
./query_parser
```

Supports: `SELECT * FROM table WHERE expr` with `AND`, `OR`, `NOT`, parentheses, and comparisons (`=`, `<`, `>`, `<=`, `>=`, `!=`).

## Part B -- Shunting-Yard Expression Evaluator

Converts infix expressions to postfix (RPN) using Dijkstra's algorithm and evaluates them with a stack.

```bash
g++ -std=c++17 -O2 -o shunting_yard shunting_yard.cpp
./shunting_yard
```

Demonstrates operator precedence (`*` before `+`, `AND` before `OR`, right-associative `^`) and step-by-step traces.

## Build Both at Once

```bash
make
make run
```

## Documentation

See [Assignment.md](Assignment.md) for background, grammar, algorithm details, and sample output.

Praveen Kumar
24bcs10048
