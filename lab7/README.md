# Lab 7: Shunting-Yard In-Memory SQL Execution Subsystem

**Author:** Manav Pratap Singh  
**Roll No.:** 24BCS10126

---

## Executive Summary

The main goal of this work is to illustrate how database engines interpret, analyze, and execute structured queries against their own internal representations of data.

Instead of dynamically analyzing the complicated `WHERE` clauses using recursive procedures, the database engine uses **Dijkstra's Shunting Yard algorithm** for translating the infix notation of the logical expressions to **Reverse Polish Notation (RPN)** only once while parsing the query. This way, the evaluation process can be done by the execution engine in `O(T)` for each record in a loop using an execution stack.

The demonstration domain will be a **Smartphone Inventory Database** that includes integer (RAM, Price) and string fields (Model, Operating System).

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