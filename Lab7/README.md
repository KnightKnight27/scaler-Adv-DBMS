# Advanced DBMS Lab 7: SQL Query Parsing & Evaluation

> **Author:** Anushka Jain (24BCS10193)
> **Course:** Advanced Database Management Systems
> **Language:** C++17

Two implementations for parsing and evaluating a SQL WHERE clause.

## Implementations

### 1. Dijkstra's Shunting-Yard (./shunting_yard/)
Converts WHERE clause from infix to RPN, evaluates with a stack.
- Pros: Fast, lean, no complex tree structures.
- Cons: Flat; cannot carry metadata.

### 2. Recursive-Descent Parser (./queryparsing/)
Builds an AST with precedence baked into grammar rules, walks it recursively.
- Pros: Structured, carries metadata. Matches how real DBMS planners work.
- Cons: More complex; requires tree traversal.

## Quick Start

Navigate to either folder and run:
  make       # compile
  make run   # compile and run
  make clean # clean up