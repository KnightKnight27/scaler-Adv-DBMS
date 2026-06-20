# Lab 7 - Shunting-Yard and Minimal SQL Parser

## Objective

Implement two ways a database front-end can process a simple `WHERE` clause:

1. Dijkstra's Shunting-Yard algorithm converts infix predicates to postfix and evaluates them with a stack.
2. A small lexer and recursive-descent parser build an expression tree for a `SELECT ... FROM ... WHERE ...` query and execute it over in-memory rows.

## Files

| File | Purpose |
|------|---------|
| `shunting_yard.cpp` | Tokenizes a SQL-style predicate, converts it to postfix, and evaluates it against student rows. |
| `query_parser.cpp` | Parses a minimal `SELECT` query with projection and boolean filters, then prints matching rows. |
| `CMakeLists.txt` | Builds both lab executables. |

## Build and Run

```bash
cd lab7
g++ -std=c++17 -Wall -Wextra -pedantic -o lab7_shunting_yard shunting_yard.cpp
./lab7_shunting_yard

g++ -std=c++17 -Wall -Wextra -pedantic -o lab7_query_parser query_parser.cpp
./lab7_query_parser
```

Or with CMake:

```bash
cmake -S lab7 -B lab7/build
cmake --build lab7/build
./lab7/build/lab7_shunting_yard
./lab7/build/lab7_query_parser
```

## Takeaways

- Infix expressions need precedence and parenthesis handling before they are safe to evaluate.
- Postfix expressions encode evaluation order directly, so one stack is enough at runtime.
- A recursive-descent parser encodes precedence in the grammar and produces an AST-like tree, which is closer to what a query planner consumes.
