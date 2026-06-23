# Lab 5: Shunting-Yard Algorithm + Minimal SQL SELECT Parser

## What this lab covers

1. Dijkstra's Shunting-Yard algorithm — converts infix expressions to RPN for WHERE clause evaluation
2. Minimal SQL SELECT parser that handles WHERE, ORDER BY, and LIMIT over an in-memory `vector<Row>`

## Build & Run

```bash
g++ -std=c++17 -o sql_parser sql_parser.cpp && ./sql_parser
```

## Expected output

```
Expression : age * 2 + salary / 1000 > 100
RPN        : age 2 * salary 1000 / + 100 >
Result     : true

SQL: SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3
gpa=3.8  id=1  name=Alice
gpa=3.5  id=3  name=Carol
gpa=3.1  id=4  name=Dave

SQL: SELECT * FROM students WHERE age >= 22 && age <= 26
id=1  name=Alice  age=22  gpa=3.8
id=2  name=Bob    age=25  gpa=2.9
```

## How it connects to a real database

```
SQL string
   |
Lexer / Tokenizer     <- tokenize()
   |
Parser                <- parse_select()  -> SelectQuery AST
   |
Planner               <- (not implemented; would choose index vs full scan)
   |
Executor              <- execute()  filters via Shunting-Yard eval, projects, sorts, limits
   |
Result set            <- vector<Row>
```

## Key insight

Shunting-Yard converts infix to RPN in O(n) with a single stack — no recursion, no AST node allocation per operator. In a real DB, the planner compiles the WHERE clause into an expression tree once; the executor evaluates it per row. `std::variant<double, std::string>` handles the typed column values cleanly in C++17.
