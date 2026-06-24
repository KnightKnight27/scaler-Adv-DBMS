# Lab 7: Shunting-Yard In-Memory SQL Execution Subsystem

**Author:** Pratham Onkar Singh  
**Roll No.:** 24bcs10136

---

## Executive Summary

The primary objective is to demonstrate how relational database engines parse, compile, and execute structured query language commands against internal data structures.

Rather than evaluating complex `WHERE` clause conditions on the fly using recursion, the engine implements **Dijkstra's Shunting-Yard Algorithm** to compile infix logical predicates into **Reverse Polish Notation (RPN)** once during query parsing. This allows the execution engine to stream through records and evaluate predicate conditions in `O(T)` time per row using a simple execution stack.

The demonstration domain models a **Smartphone Inventory Database** supporting integer metrics (RAM, Price) and textual attributes (Model, Operating System).

---

## System Architecture Pipeline

The query execution flow follows four distinct phases:

1. **Lexical Tokenization (`tokenizeSQL`):** Raw SQL text is scanned character-by-character and decomposed into a strongly-typed stream of lexemes (Keywords, Identifiers, Literals, Operators, and Parentheses).
2. **Predicate Compilation (`shuntingYardRPN`):** The `WHERE` clause expression stream is transformed from standard human-readable Infix notation into Postfix (RPN). Operator precedence (e.g., `<` before `AND` before `OR`) and grouping parentheses are resolved statically.
3. **Query Plan Generation (`compileQuery`):** The token stream is validated against a formal `SELECT` grammar, generating an executable query plan containing projected columns, the source relation, the compiled RPN condition stack, sorting instructions, and truncation limits.
4. **Relational Execution (`executeQuery`):** \* **Filtering:** Iterates through tuples, pushing cell values onto an evaluation stack against the RPN predicate. Tuples yielding `false` are dropped.
   - **Projection:** Strips away unrequested table columns.
   - **Sorting:** Applies a stable sort ordered by the requested column.
   - **Limitation:** Resizes the final result set to satisfy `LIMIT` constraints.

---

## Shunting-Yard Predicate Compilation Example

When the engine encounters a complex logical query:

- **Raw Infix Predicate:** `ram > 8 AND (os = 'iOS' OR price >= 80000)`

- **Compiled Postfix (RPN) Stack:** `ram 8 > os 'iOS' = price 80000 >= OR AND`

During row execution, operands are pushed onto an evaluation stack. Encountering an operator instantly pops the required operands, computes the boolean truth value, and pushes the result back onto the stack.

---

## Supported Grammar & Operators

### SQL Syntax

    SELECT [ * | col1, col2, ... ]
    FROM table_name
    [WHERE logical_expression]
    [ORDER BY sort_column ASC|DESC]
    [LIMIT integer]

### Supported Operators (By Precedence)

1. **Relational (Highest):** `=`, `!=`, `<`, `<=`, `>`, `>=`
2. **Logical Unary:** `NOT`
3. **Logical Binary:** `AND`
4. **Logical Binary (Lowest):** `OR`

---

## Build & Execution Instructions

A POSIX-compliant `Makefile` is provided for streamlined compilation.

**To compile and run the inventory engine immediately:**

    make run

**To compile manually:**

    c++ -std=c++17 -O3 -Wall -Wextra main.cpp sql_engine.cpp -o mini_sql_engine
    ./mini_sql_engine

**To clean build artifacts:**

    make clean
