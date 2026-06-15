<div align="center">

# 🧮 Lab Session 5: Shunting-Yard Algorithm + Minimal SQL SELECT Parser over `vector<Row>`
### Implementing Infix Expression Evaluators & In-Memory Relational Query Engines

[![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://www.kernel.org/)

</div>

---

## 👨‍🎓 Student Details
- **Name:** Siddhant Prasad
- **Roll Number:** 24BCS10255

---

## 🎯 Objective
1. Implement Dijkstra's Shunting-Yard algorithm to parse and evaluate infix arithmetic and boolean expressions (essential for SQL WHERE clause evaluation).
2. Build a minimal SQL parser that processes `SELECT` queries (including columns, `WHERE`, `ORDER BY`, and `LIMIT` clauses) and executes them against an in-memory `vector<Row>` dataset.

---

## 📚 Part 1: Shunting-Yard Expression Evaluator

SQL WHERE clauses contain infix boolean expressions such as:
$$\text{age} \times 2 + \text{salary} / 1000 > 100$$
To evaluate this efficiently, Dijkstra's **Shunting-Yard algorithm** converts the expression from infix notation (operator between operands) to postfix notation (Reverse Polish Notation or RPN, operator after operands). Postfix expressions can then be evaluated in linear time $O(n)$ using a single value stack without needing recursive descent.

### Operator Precedence Rules
Precedence levels dictate the parsing order:
- High precedence: exponentiation (`^`), multiplication/division (`*`, `/`).
- Mid precedence: addition/subtraction (`+`, `-`), comparison operators (`<`, `>`, `=`, `!=`).
- Low precedence: logical operators (`&&`, `||`).

---

## 💻 Part 2: Minimal SQL SELECT Parser over `vector<Row>`

We define a minimal relational engine in C++:
1. **Row & Value Representation**: A row is modeled as a hash map mapping column names to dynamic `Value` variants (`std::variant<double, std::string>`).
2. **Parser**: A case-insensitive tokenizer parses incoming SQL strings into an Abstract Syntax Tree structure (`SelectQuery`).
3. **Execution Plan**:
   - **Filter**: Iterate over the dataset and evaluate the parsed `WHERE` expression using the Shunting-Yard evaluator.
   - **Project**: Select only the columns specified in the `SELECT` clause (or all if `*` is specified).
   - **Sort**: Reorder matching rows based on the `ORDER BY` column (ascending or descending).
   - **Truncate**: Restrict the number of returned rows based on the `LIMIT` value.

### File Location
The code is written to [sql_parser.cpp](file:///c:/Users/Siddhant/OneDrive/Desktop/scaler-Adv-DBMS/Lab_5/sql_parser.cpp).

### Compile and Run
```bash
g++ -std=c++17 sql_parser.cpp -o sql_parser
./sql_parser
```

---

## 🗺️ How the Pieces Connect in a Real Database

```mermaid
graph TD
    A["SQL Query String (SELECT id, name FROM students WHERE gpa > 3.0)"] --> B["Lexer / Tokenizer (Extracts words, identifiers, operators)"]
    B --> C["Parser (Builds SelectQuery AST)"]
    C --> D{Query Planner}
    D -- Index Scan --> E[Index Access Path]
    D -- Seq Scan --> F[Sequential Scan Access Path]
    E --> G[Storage Engine (Reads pages from disk)]
    F --> G
    G --> H["Executor (Iterates rows, projects columns, filters via Shunting-Yard RPN)"]
    H --> I[Result Set Output]
```

- **Expression Trees**: In production systems (like PostgreSQL), the planner compiles WHERE clauses into optimized expression execution trees during planning, rather than re-parsing strings for every processed row. The Shunting-Yard algorithm operates similarly to build these expression trees.
- **Buffer Pool Interaction**: The `vector<Row>` processed in this lab represents the records fetched from disk pages (analyzed in Labs 2 and 3).
- **Sort nodes**: `ORDER BY` operations map to sorting nodes in the physical query execution plan.

---

## 🏁 Key Takeaways
- **Stack-based Evaluation**: The Shunting-Yard algorithm parses infix expressions into RPN in $O(n)$ time using an operator stack, avoiding complex recursive parsing.
- **Precedence & Associativity**: Operator precedence and associativity (e.g., right-associativity for exponentiation `^`) determine the exact ordering of RPN outputs.
- **Relational Operations**: A minimal SQL executor consists of four pipeline stages: Filtering (`WHERE`), Projection (`SELECT` fields), Sorting (`ORDER BY`), and Truncation (`LIMIT`).
- **Dynamic Typing**: Storing database values as `std::variant` allows C++ to handle numeric and string types safely in memory.
