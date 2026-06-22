# LAB 5 — SHUNTING-YARD ALGORITHM + MINIMAL SQL SELECT PARSER

> Roll Number: **10075** &nbsp;&nbsp;|&nbsp;&nbsp; Name: **Nase Anishka**

This lab implements Dijkstra's Shunting-Yard algorithm to evaluate infix arithmetic/boolean expressions (the mechanism behind SQL WHERE clause evaluation), and builds a minimal SQL parser that executes SELECT queries against an in-memory `vector<Row>`.

---

## WHAT I BUILT

### Part 1 — Shunting-Yard Expression Evaluator
Converts infix expressions to RPN (Reverse Polish Notation) using a stack, then evaluates the RPN with a second stack. Supports arithmetic (`+`, `-`, `*`, `/`, `^`), comparison (`<`, `>`, `<=`, `>=`, `=`, `!=`), and logical (`&&`, `||`) operators with correct precedence and right-associativity for `^`.

### Part 2 — Minimal SQL SELECT Parser
Parses `SELECT col,... FROM table [WHERE expr] [ORDER BY col [ASC|DESC]] [LIMIT n]` into a `SelectQuery` struct, then executes it against a pre-fetched `vector<Row>`. WHERE evaluation reuses the Shunting-Yard evaluator directly — exactly how real query executors work.

---

## FILES IN THIS FOLDER

| File | Purpose |
|------|---------|
| `main.cpp` | Shunting-Yard (Part 1) + SQL parser + executor (Part 2) |
| `CMakeLists.txt` | CMake build config |
| `.gitignore` | Ignore build artefacts |
| `run_output.txt` | Captured output from a real run |
| `README.md` | This file |

---

## HOW TO BUILD AND RUN

```bash
# Direct g++
g++ -std=c++17 -Wall -o sql_parser main.cpp && ./sql_parser

# CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/sql_parser
```

---

## PART 1: SHUNTING-YARD — DEEP DIVE

### The algorithm
Input: infix token stream from `tokenize()`.  
Output: RPN (postfix) token list from `to_rpn()`.  
Evaluation: `eval_rpn()` with a `double` stack.

The key invariant: when an operator o1 is seen, pop all stack operators o2 with `precedence(o2) > precedence(o1)` (or equal precedence and o1 is left-associative) into the output before pushing o1.

### Operator table

| Operator | Precedence | Associativity |
|----------|------------|---------------|
| `\|\|` | 1 | Left |
| `&&` | 2 | Left |
| `=`, `!=` | 3 | Left |
| `<`, `>`, `<=`, `>=` | 4 | Left |
| `+`, `-` | 5 | Left |
| `*`, `/` | 6 | Left |
| `^` | 7 | **Right** |

### Demo output

```
Expression : age * 2 + salary / 1000 > 100
RPN        : age 2 * salary 1000 / + 100 >
Variables  : age=30, salary=50000
Result     : true

Expression : (3 + 4) * 2 ^ 3 - 1
RPN        : 3 4 + 2 3 ^ * 1 -
Result     : 55

Expression : age >= 18 && gpa > 3.0
RPN        : age 18 >= gpa 3.0 > &&
Variables  : age=22, gpa=3.5
Result     : true
```

For `(3 + 4) * 2 ^ 3 - 1`:
- `^` is right-associative — `2 ^ 3 = 8`
- Then `(3+4) * 8 - 1 = 7 * 8 - 1 = 55`

---

## PART 2: SQL PARSER — DEEP DIVE

### Query structure
```cpp
struct SelectQuery {
    vector<string> columns;   // empty = SELECT *
    string         from;
    string         where_raw; // raw WHERE clause, evaluated via Shunting-Yard
    string         order_by;
    bool           order_asc = true;
    int            limit     = -1;
};
```

### Execution pipeline
```
SQL string → parse_select() → SelectQuery
                                   ↓
                             execute(q, data)
                              ├─ tokenize(where_raw) → to_rpn() → eval_rpn() per row (WHERE filter)
                              ├─ column projection (SELECT)
                              ├─ std::sort by order_by column (ORDER BY)
                              └─ result.resize(limit) (LIMIT)
```

### Demo output

```
SQL: SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3
id=1  name=Alice  gpa=3.8
id=3  name=Carol  gpa=3.5
id=4  name=Dave   gpa=3.1

SQL: SELECT * FROM students WHERE age >= 22 && age <= 26
id=1  name=Alice  age=22  gpa=3.8
id=2  name=Bob    age=25  gpa=2.9

SQL: SELECT name, age FROM students WHERE gpa >= 3.5 ORDER BY age ASC
name=Carol  age=21
name=Alice  age=22
```

### How this connects to a real database

```
SQL string
   │
Tokenizer       ← tokenize()
   │
Parser          ← parse_select()   produces SelectQuery (a minimal AST)
   │
Planner         ← (not implemented) would choose index vs full scan
   │
Executor        ← execute()  iterates vector<Row>, evaluates WHERE via Shunting-Yard
   │
Result set      ← vector<Row>
```

- In a real DB the WHERE expression is compiled into an expression tree by the planner. Shunting-Yard is the algorithm that builds that tree.
- The `vector<Row>` simulates the output of the storage layer from Labs 2-3 (page reads off disk).
- ORDER BY maps to a sort node in the query plan (PostgreSQL: `Sort` node in `EXPLAIN`).

---

## KEY TAKEAWAYS

- Shunting-Yard converts infix to RPN in O(n) with a single stack — no recursion, no backtracking.
- Operator precedence and right-associativity are the two properties that determine where operators land in the output.
- A minimal SQL executor is: filter (WHERE) → project (columns) → sort (ORDER BY) → truncate (LIMIT). Each step is a pure function over `vector<Row>`.
- Every production SQL database uses the same conceptual pipeline; the differences are in how the planner chooses algorithms (hash join vs nested loop, index scan vs seq scan) for each step.
