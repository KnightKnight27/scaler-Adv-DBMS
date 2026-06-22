# Lab 7: Shunting-Yard Logical Evaluator & SQL SELECT Parser

## 1. Overview
This lab implements:
1. **Dijkstra's Shunting-Yard Algorithm:** Translates mathematical/logical infix strings (e.g. `age > 30 AND city == 'Mumbai'`) into Postfix (Reverse Polish Notation).
2. **Postfix Boolean Evaluator:** Evaluates Postfix expressions against single table rows by substituting variables with actual column values.
3. **Minimal SQL SELECT Parser:** Extracts target columns, the source table, and the filter conditions from a query string, runs the evaluator, and projects the matching columns.

---

## 2. Architecture & Design

### Shunting-Yard Infix to Postfix Parser
Logical and comparison operations contain operator hierarchies. Shunting-Yard utilizes an operator stack to reorganize infix queries based on operator precedence:

| Operator | Precedence Level | Description |
| :--- | :--- | :--- |
| `==`, `!=`, `>`, `<`, `>=`, `<=` | **3** | Relational comparisons (Highest Precedence) |
| `AND` | **2** | Logical Conjunction |
| `OR` | **1** | Logical Disjunction (Lowest Precedence) |

#### Shunting-Yard Flow:
- **Operand (Variables/Literals):** Appended directly to the output postfix stream.
- **Left Parenthesis `(`:** Pushed to the operator stack.
- **Right Parenthesis `)`:** Operators are popped from the stack and appended to the output stream until a matching `(` is encountered.
- **Operator:** Pops operators from the stack that have equal or higher precedence than the current operator, appends them to the postfix stream, and then pushes the current operator.

---

## 3. SQL Query Execution Engine

### Query Syntax
Our database engine parses standard queries matching:
```sql
SELECT column1, column2 FROM tableName WHERE condition
```

### Parsing Pipeline
1. **Tokenization:** Splits the raw input string into logical chunks (e.g., separating variables, comparison operators, string constants, and parentheses).
2. **Metadata Separation:** Extract the list of projection columns, target table name, and raw WHERE condition substring.
3. **RPN Conversion:** Run the Shunting-Yard compiler over the WHERE tokens to produce the Reverse Polish Notation (RPN) array.
4. **Scan & Filter:** Iterate through the `std::vector<Row>` array. For each row:
   - Substitute variable tokens (like `city`) with their values from the row (like `"Mumbai"`).
   - Evaluate the RPN tree.
   - If the evaluation returns `true`, select the specified columns and print them to the output.

---

## 4. Building and Running

### Compilation
Compile the program using `g++` (requires C++11 or higher):
```bash
g++ -std=c++17 -Wall main.cpp -o db_parser
```

### Execution
Run the compiled binary:
```bash
./db_parser
```

### Expected Output
The program defines a database table `users` containing fields `id`, `name`, `age`, and `city`, and outputs filtered tables for several SQL queries:
- Querying for `city == 'Mumbai'` (outputs rows for Mehul, Rahul, Karan).
- Querying for `age > 30` (outputs rows for Amit, Rahul, Sneha, Karan).
- Multi-condition logical evaluation: `city == 'Mumbai' AND age > 30` (outputs Rahul and Karan).
