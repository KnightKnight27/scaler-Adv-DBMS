# Lab Session 5: Shunting-Yard Algorithm + Minimal SQL SELECT Parser over vector<Row>

## Objective
1. Implement Dijkstra's Shunting-Yard algorithm to evaluate infix arithmetic/boolean expressions (used in SQL WHERE clause evaluation).
2. Build a minimal SQL parser that handles SELECT queries and executes them against an already-fetched `vector<Row>` in memory.

---

# Part 1: Shunting-Yard Algorithm (Expression Evaluator)

## Background
SQL WHERE clauses contain infix expressions like `age > 25 AND salary * 1.1 < 90000`. The shunting-yard algorithm converts infix → postfix (RPN), which can be evaluated with a simple stack.

The source code is located in [sql_parser.cpp](file:///C:/Users/singh/Downloads/scaler-Adv-DBMS-main/scaler-adv-dmbs/lab5/sql_parser.cpp).

---

# Part 2: Minimal SQL SELECT Parser over vector<Row>

## Query Execution Order & Projection Design Fix
In the executor implementation, we moved column projection to the very end of the execution pipeline:
1. **Filter** (Evaluate WHERE via Shunting-Yard on original rows)
2. **Sort** (ORDER BY sorting on original rows)
3. **Limit** (Reduce results count)
4. **Project** (Select final fields)

**Why?** In the naive code version, projection was done before sorting. This meant if you queried `SELECT id, name FROM students ORDER BY gpa DESC`, the projection step would strip out the `gpa` column before the sorting step could access it, breaking the sorting. Performing projection at the very end ensures the sort keys are fully preserved during execution.

Compile and run:
```bash
g++ -std=c++17 -o sql_parser sql_parser.cpp
./sql_parser
```

---

## How the pieces connect to a real database

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

- In a real DB, the WHERE expression is compiled into an expression tree by the planner, not re-parsed per row. Shunting-Yard is the mechanism that builds that tree.
- The `vector<Row>` in this lab simulates the output of a storage layer that already fetched pages from disk (as in Labs 2-3).
- ORDER BY maps to a sort on the result buffer — in PostgreSQL this is a sort node in the query plan.

---

## Key Takeaways
- Shunting-Yard converts infix to RPN in O(n) with a stack — no recursion needed.
- Operator precedence and associativity are the two properties that determine output order.
- A minimal SQL executor is just: filter (WHERE) → project (SELECT columns) → sort (ORDER BY) → truncate (LIMIT).
- String-column support requires treating values as typed variants, not raw doubles — `std::variant` makes this clean in C++17.
