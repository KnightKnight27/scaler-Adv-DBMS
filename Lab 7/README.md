# Lab Session 7: Shunting-Yard Algorithm + Minimal SQL SELECT Parser over vector<Row>

**Roll No.:** 24BCS10318
**Name:** Utkarsh Raj

## Objective
1. Implement Dijkstra's Shunting-Yard algorithm to evaluate infix arithmetic/boolean expressions (used in SQL `WHERE` clause evaluation).
2. Build a minimal SQL parser that handles `SELECT` queries and executes them against an already-fetched `vector<Row>` in memory.

---

## Part 1: Shunting-Yard Algorithm (Expression Evaluator)

### Background
SQL `WHERE` clauses contain infix expressions like `age > 25 AND salary * 1.1 < 90000`. The shunting-yard algorithm converts infix $\to$ postfix (RPN), which can be evaluated with a simple stack.

### Precedence Table
- `||`: Precedence 1, Left-Associative
- `&&`: Precedence 2, Left-Associative
- `=`, `!=`: Precedence 3, Left-Associative
- `<`, `>`, `<=`, `>=`: Precedence 4, Left-Associative
- `+`, `-`: Precedence 5, Left-Associative
- `*`, `/`: Precedence 6, Left-Associative
- `^`: Precedence 7, Right-Associative

---

## Part 2: Minimal SQL SELECT Parser & Executor over `vector<Row>`

### Schema & Row type
Rows are represented in memory as a mapping from column names (`std::string`) to variants supporting both numeric (`double`) and string values (`std::string`):
```cpp
using Value = std::variant<double, std::string>;
struct Row {
    std::unordered_map<std::string, Value> cols;
};
```

### Parser & Executor Flow
```
SQL string
   |
Lexer / Tokenizer      <- tokenize()
   |
Parser                 <- parse_select()  produces a SelectQuery AST
   |
Planner                <- (not implemented) would choose index vs full scan
   |
Executor               <- execute()  iterates vector<Row>, evaluates WHERE via Shunting-Yard
   |
Result set             <- vector<Row>
```

---

## Compilation and Execution

### Direct Compilation with g++
From the `Lab 7` directory:
```bash
g++ -std=c++17 -o sql_parser sql_parser.cpp
./sql_parser
```

### Using CMake
From the workspace root directory:
```bash
cmake -B "Lab 7/build" -S "Lab 7"
cmake --build "Lab 7/build"
```

To run:
- On Windows:
  ```bash
  ./"Lab 7/build/sql_parser.exe"
  ```
- On Linux/macOS:
  ```bash
  ./"Lab 7/build/sql_parser"
  ```

---

## Key Takeaways
- **Shunting-Yard**: Converts infix to RPN in $O(n)$ time using a stack (avoiding recursion).
- **Operator Precedence**: Dictates execution order when evaluating SQL `WHERE` clauses.
- **Minimal SQL Executor**: Composed of filter (`WHERE` evaluation) $\to$ project (select columns) $\to$ sort (`ORDER BY`) $\to$ truncate (`LIMIT`).
