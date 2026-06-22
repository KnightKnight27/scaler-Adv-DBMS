# Lab 7: SQL Query Tokenizer & Stack-Based Postfix Evaluator

> **Course:** Advanced Database Management Systems (ADBMS)  
> **Student Name:** Nandani Kumari  
> **Roll Number:** 24bcs10317  
> **Language Platform:** C++17  

---

## 1. Abstract

This lab implements a query scanning and evaluation subsystem in C++17. The program performs lexical parsing on standard SQL `SELECT` statements, converts logical filter conditions from standard infix notation to **Reverse Polish Notation (RPN)** via Dijkstra's **Shunting-Yard algorithm**, and evaluates the conditions directly against database records using a post-order stack execution method.

This approach demonstrates how databases optimize simple selection routines without the overhead of heavy tree-building abstractions.

---

## 2. Deliverables List

| Component | Responsibility |
| :--- | :--- |
| `main.cpp` | Complete database query processor containing scanner, parser, evaluator, and sample run inputs. |
| `CMakeLists.txt` | Standard project configuration. |
| `README.md` | This design documentation and manual. |

---

## 3. Compilation & Execution

To compile and launch the evaluation test cases, run:

```bash
# Configure and compile using CMake
cmake -S . -B build
cmake --build build

# Launch the executable
./build/sql_eval_test
```

---

## 4. Subsystem Pipelines

### Pipeline 1: Lexical Scan (`scanQuery`)
Translates query strings into discrete tokens:
* Brackets (`SYM_LPAREN`, `SYM_RPAREN`)
* Literals (`SYM_NUMBER`, `SYM_STRING`)
* Operators (`SYM_OPERATOR` like `=`, `>`, `!=`)
* Command statements (`SYM_KEYWORD` like `SELECT`, `FROM`, `WHERE`)

### Pipeline 2: Postfix Processing (`parseToPostfix`)
Converts mathematical and logical filter constraints to Postfix (RPN):
* Example: `(genre = 'Fiction' OR genre = 'Dystopian') AND stock >= 100` becomes:
  `genre 'Fiction' = genre 'Dystopian' = OR stock 100 >= AND`

### Pipeline 3: Stack-based Evaluation (`runPostfixFilter`)
Evaluates the postfix token queue:
* Pushes literal and row values onto the stack.
* Evaluates logical conditions (`AND`, `OR`) and arithmetic comparison operators directly against stack operands.

---

## 5. Dataset Schema & Scenarios

The engine runs query scenarios against a dataset of `books` with attributes: `id`, `title`, `genre`, `price`, and `stock`.

### Program Scenarios Evaluated:
1. **Selection on Numeric Conditions and Logical AND**:
   `SELECT title, price FROM books WHERE genre = 'Science' AND price > 20.00`
2. **Inequality Check**:
   `SELECT title, genre FROM books WHERE genre != 'Fiction'`
3. **Compound Range Check**:
   `SELECT title, stock, genre FROM books WHERE stock >= 40 AND stock <= 150`
4. **Precedence Override (Parentheses and Logical OR)**:
   `SELECT title, genre, stock FROM books WHERE (genre = 'Fiction' OR genre = 'Dystopian') AND stock >= 100`
5. **Wildcard Selection**:
   `SELECT * FROM books`
