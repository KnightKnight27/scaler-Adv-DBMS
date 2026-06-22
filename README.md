# Scaler Advanced DBMS Labs

This repository contains implementations of advanced database management system features.

## Lab Session 5: Shunting-Yard Algorithm + Minimal SQL SELECT Parser

Implements Dijkstra's Shunting-Yard algorithm to evaluate infix arithmetic/boolean expressions and builds a minimal SQL parser that handles SELECT queries and executes them against an already-fetched `vector<Row>` database in memory.

### Features
1. **Shunting-Yard Expression Evaluator**:
   * Tokenizes infix expression strings (numbers, identifiers, operators, parentheses).
   * Converts infix tokens to Reverse Polish Notation (RPN) / Postfix notation using operator precedence and associativity.
   * Evaluates the RPN stack against a row's column values (cast to double).
2. **Minimal SQL Parser**:
   * Tokenizes SQL keyword strings case-insensitively.
   * Safely handles comma-separated columns (with or without spaces).
   * Parses `SELECT`, `FROM`, `WHERE`, `ORDER BY` (with `ASC`/`DESC` directions), and `LIMIT` clauses.
3. **Relational Query Executor**:
   * Resolves query steps in standard database execution order:
     `Filter (WHERE) -> Sort (ORDER BY) -> Limit (LIMIT) -> Project (SELECT)`
   * This execution order allows sorting based on attributes not present in the projection list.

### Compilation and Execution

Compile `sql_parser.cpp` with C++17 or higher and run the executable:

```powershell
# Compile the program
g++ -std=c++17 -o sql_parser sql_parser.cpp

# Run the program
./sql_parser
```
