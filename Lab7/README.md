# Lab 7: SQL Query Lexer & AST-Free RPN Parser Engine

> **Subject:** Advanced Database Management Systems (ADBMS)  
> **Student Name:** Ayush Kumar Patra  
> **Roll Number:** 24bcs10474  
> **Language:** C++17  

---

## 1. Overview & Mechanics

This lab features a ground-up C++17 execution runtime that lexically tokenizes SQL-like `SELECT` queries, compiles query filter expressions into **Reverse Polish Notation (RPN)** via Dijkstra's **Shunting-Yard algorithm**, and executes the filter directly against in-memory rows. 

By executing operators from an evaluation stack, the engine avoids the overhead of constructing a full Abstract Syntax Tree (AST), demonstrating low-level query optimization and scheduling techniques.

---

## 2. Directory Contents

| Component | Responsibility |
| :--- | :--- |
| `main.cpp` | Integrated C++ source code containing the tokenizer, Shunting-Yard converter, RPN filter evaluator, and test execution driver. |
| `CMakeLists.txt` | Standard compilation specification using standard parameters. |
| `README.md` | This technical summary and user manual. |

---

## 3. How to Compile & Run

To build the executable and execute the test scenarios:

```bash
# Compile and create the target binary
cmake -S . -B build
cmake --build build

# Execute the binary
./build/query_engine
```

---

## 4. Query Parsing Stages

### Step 1: Lexical Analysis (`tokenize`)
Identifies types such as brackets (`L_PAREN`, `R_PAREN`), literals (`NUMBER`, `LITERAL_STR`), operators (`OPERATOR`), and keywords (`KEYWORD` like `SELECT`, `WHERE`, `AND`, `OR`).

### Step 2: Postfix Parsing (`shuntingYard`)
Converts the `WHERE` expression from infix representation into postfix (Reverse Polish Notation).
* For example, `(major = 'Biology' OR major = 'History') AND gpa >= 3.2` becomes:
  `major 'Biology' = major 'History' = OR gpa 3.2 >= AND`

### Step 3: Stack-based Evaluation (`executeFilter`)
Reads the RPN tokens sequentially:
* Pushes values and column values onto a value stack.
* Pops operands when encountering operators, computes the result, and pushes it back.
* Returns whether the final stack value equals true (`1`).

---

## 5. Dataset & Scenarios

The engine operates on a database relation named `students` with the following attributes:
* `id`
* `name`
* `major`
* `gpa`
* `credits`

### Scenario Tests Run:
1. **Selection on Numeric Conditions and Logical AND**:
   `SELECT name, gpa FROM students WHERE major = 'Computer Science' AND gpa > 3.6`
2. **Inequality Check**:
   `SELECT name, major FROM students WHERE major != 'History'`
3. **Compound Range Check**:
   `SELECT name, credits, major FROM students WHERE credits >= 50 AND credits <= 100`
4. **Precedence Override (Parentheses and Logical OR)**:
   `SELECT name, major, gpa FROM students WHERE (major = 'Biology' OR major = 'History') AND gpa >= 3.2`
5. **Wildcard Selection**:
   `SELECT * FROM students`
