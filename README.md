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

---

## Lab Session 6: Concurrency Control Transaction Manager (MVCC + Strict 2PL)

Implements a thread-safe database transaction manager that combines Multi-Version Concurrency Control (MVCC) snapshot isolation, Strict Two-Phase Locking (2PL), and waits-for graph cycle detection for deadlock prevention.

### Features
1. **MVCC Snapshot Isolation**:
   * Every write (Insert, Update, Delete) creates a new row version in a linked list version chain.
   * Readers check visibility rules using transaction start/snapshot boundaries without holding read locks that block writers.
   * Rollback undoes inserts (marking them invisible) and restores previous versions modified by deletes/updates.
2. **Lock Manager (Strict 2PL)**:
   * Acquires `SHARED` (for reads) and `EXCLUSIVE` (for writes) locks on row keys.
   * Binds lock acquisition to the growing phase of the transaction (Strict 2PL: locks are held until transaction commit/abort).
   * Fully thread-safe design protecting global maps from data races and reference invalidations.
3. **Deadlock cycle checker**:
   * Tracks active blockers and constructs a waits-for dependency graph.
   * Runs recursive depth-first search (DFS) checks on lock wait requests to abort transactions completing a cycle.

### Compilation and Execution

Compile `txmgr.cpp` with C++17 or higher and run the executable:

```powershell
# Compile the program
g++ -std=c++17 -pthread -o txmgr txmgr.cpp

# Run the program
./txmgr
```

