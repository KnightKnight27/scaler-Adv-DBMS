# Lab Session 5 - Shunting-Yard Algorithm and SQL Parser

Name: Amitabh Panda
Roll No: 24BCS10104

## Objective

This lab explores two important concepts used in database query processing:

1. Shunting-Yard Algorithm
2. Minimal SQL SELECT Parser

The Shunting-Yard Algorithm is used to convert infix expressions into postfix notation for efficient evaluation.

The SQL Parser demonstrates how a database interprets and executes SELECT statements using filtering, projection, sorting, and limiting operations.

---

## Project Structure

Lab5/
│
├── README.md
│
├── shunting_yard/
│   └── shunting_yard.cpp
│
└── sql_parser/
    └── sql_parser.cpp

---

## Compilation

### Shunting-Yard

g++ -std=c++17 shunting_yard.cpp -o shunting

./shunting

### SQL Parser

g++ -std=c++17 sql_parser.cpp -o sqlparser

./sqlparser

---

## Learning Outcomes

- Expression parsing
- Operator precedence handling
- Reverse Polish Notation (RPN)
- SQL query parsing
- WHERE clause evaluation
- ORDER BY processing
- LIMIT handling
- Basic query execution pipeline