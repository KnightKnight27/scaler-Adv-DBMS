# Lab 5: Dijkstra's Shunting-Yard Expression Evaluator & SQL SELECT Parser

**Name:** Rachit S  
**Roll Number:** 24bcs10139  
**Course:** Advanced Database Management Systems (AdvDBMS)

---

## 1. Overview
Database query processors compile SQL command strings into execution pipelines. This lab demonstrates:
1. **Dijkstra's Shunting-Yard Algorithm:** Translates infix conditional expressions (such as standard SQL `WHERE` clauses like `age > 25 AND gpa < 3.0`) into Reverse Polish Notation (RPN / Postfix).
2. **Postfix Stack Evaluator:** Resolves RPN conditions dynamically against in-memory rows.
3. **Query Engine:** Parses and executes SELECT projections, filtering, sorting (`ORDER BY`), and limits (`LIMIT`) over standard `vector<Row>` data.

---

## 2. Infix to Postfix (Shunting-Yard) Workflow

```
   Infix Input: "age > 25 AND gpa < 3.0"
                       │
                       ▼
            [Shunting-Yard Stack]
                       │
                       ▼
  Postfix (RPN): "age 25 > gpa 3.0 < &&"
```

1. **Identifiers & Literals:** Pushed directly to the output queue.
2. **Operators:** Pushed onto the operator stack. Before doing so, any operator on top of the stack with higher precedence or equal precedence (if left-associative) is popped to the output queue.
3. **Parentheses:** `(` is pushed onto the stack. `)` triggers popping operators from the stack to the output queue until a matching `(` is encountered.

---

## 3. SQL Parser and Execution pipeline

```
 SQL Query String 
       │
       ▼
  Tokenizer       --> Split into keyword/operator tokens
       │
       ▼
   AST Parser     --> Extracts columns, WHERE expression, ORDER BY, LIMIT
       │
       ▼
    Executor      --> Iterates rows, applies filters, projects, sorts & limits
```

- **Row Storage Representation:** Evaluates row metrics dynamically using C++17 variant maps (`std::unordered_map<std::string, std::variant<double, std::string>>`).
- **Filters:** Evaluates the `WHERE` RPN expression.
- **Projection:** Strips non-selected columns from rows.
- **Sorting & Truncation:** Performs custom relational sort (`std::sort`) and vector truncation (limiting length).

---

## 4. Compilation & Execution

To compile and run the SQL parser:
```bash
g++ -std=c++17 lab5/sql_parser.cpp -o sql_test
./sql_test
```
