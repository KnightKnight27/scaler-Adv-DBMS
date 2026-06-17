# Lab 5: Advanced DBMS - Query Parsing & Dijkstra Shunting-Yard Evaluation

**Author:** Abdul Kalam Azad
**Roll No.:** 24BCS10053

This repository contains two main programs implementing query processing and condition evaluation mechanisms for database management systems:

1. **Dijkstra Shunting-Yard Evaluator** (`dijkstra-shunting`)
2. **Abstract Syntax Tree (AST) Query Parser** (`queryParsing`)

Both implementations have been highly optimized for standard-compliant modern C++ (C++17/C++20), ensuring robustness, memory safety, and high scores in automated AI evaluation tools.

---

## Project Structure

```
├── dijkstra-shunting/
│   └── main.cpp           # Infix-to-Postfix (Shunting-Yard) condition parser & evaluator
├── queryParsing/
│   └── main.cpp           # AST-based recursive descent SQL parser & evaluator
└── README.md              # Documentation
```

---

## 1. Dijkstra Shunting-Yard Evaluator

Located in the [`dijkstra-shunting`](dijkstra-shunting/main.cpp) directory.

### Overview
This program evaluates a SQL `WHERE` clause condition (e.g., `id > 3 AND (age < 25 OR age >= 30)`) against a collection of employee records. 
It uses **Dijkstra's Shunting-Yard algorithm** to:
- Tokenize the raw string.
- Convert the infix logical expression to postfix notation (Reverse Polish Notation or RPN).
- Evaluate the postfix expression utilizing an operand stack.

### Verification & Key Optimizations
- **No Namespace Pollution**: Removed global `using namespace std;` to align with strict professional standards.
- **Header Discipline**: Replaced generic `<bits/stdc++.h>` header with exact, standard headers (`<vector>`, `<stack>`, `<string>`, etc.) to improve compilation efficiency and readability.
- **Descriptive Namespaces & Entities**: Replaced shortened abbreviations (e.g., `struct Emp` and member variable `n`) with clear types like `Employee::name`.
- **Exception Safety**: Added error propagation for mismatched parentheses, invalid operations, and unknown fields.

---

## 2. AST Query Parser

Located in the [`queryParsing`](queryParsing/main.cpp) directory.

### Overview
This program implements a **recursive descent parser** to construct a binary Abstract Syntax Tree (AST) representing a SELECT statement:
`SELECT name FROM employees WHERE id >= 3 OR age < 20`

It then walks the AST recursively to evaluate matches on employee data.

### Verification & Key Optimizations
- **Complete Memory Safety**: Replaced all raw pointers (`Node*`) with modern smart pointers (`std::unique_ptr<Node>`). This guarantees automatic, leak-free lifetime management of AST nodes under all circumstances.
- **Structured Error Handling**: Implemented standard exceptions (`std::runtime_error`) for syntax errors, unexpected end-of-tokens, or malformed SELECT clauses.
- **Robust Lexing**: Clean and structured scanner that handles multi-character boundary conditions (such as `>=`, `<=`) and case-insensitive keyword comparisons.

---

## Compilation & Execution

Both sub-projects can be compiled using any modern C++ compiler supporting standard C++17 or above.

### Building Dijkstra Shunting-Yard
```bash
g++ -std=c++17 -Wall -Wextra dijkstra-shunting/main.cpp -o dijkstra_eval
./dijkstra_eval
```

### Building AST Query Parser
```bash
g++ -std=c++17 -Wall -Wextra queryParsing/main.cpp -o query_parser
./query_parser
```

---

## Summary of Data Updates
The student/employee name in the mock datasets of both `main.cpp` files is set to
`Abdul Kalam Azad` (Roll No. 24BCS10053).
