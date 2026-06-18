# Lab 7: Dijkstra's Shunting-Yard Algorithm for SQL Query Parsing

**Roll No:** 24BCS10221  
**Subject:** Advanced Database Management Systems (Lab 7)

---

## 1. Overview and Problem Statement

When writing SQL queries, the `WHERE` clause defines a set of logical and comparative filters (e.g., `id > 3 AND (age < 25 OR age >= 30)`). In standard math and coding, we write these expressions in **Infix Notation** (operators between operands).

While natural for humans, infix notation is hard for machines to evaluate directly because:
1. **Operator Precedence:** Certain operators must be evaluated before others (e.g., comparison operators bind tighter than `AND`, which binds tighter than `OR`).
2. **Parentheses:** Parenthetical groupings modify the default precedence order.

To solve this, we convert the query expression from **Infix** to **Postfix Notation** (also known as **Reverse Polish Notation / RPN**), where operators always follow their operands.

### Why Postfix (RPN)?
In RPN, **there are no parentheses and no operator precedence rules required at evaluation time.** The order of elements uniquely defines the order of evaluation. This allows a computer to evaluate complex expressions in a single pass using a stack.

| Style | Expression |
| :--- | :--- |
| **Infix** | `marks >= 80 AND id < 6` |
| **Postfix (RPN)** | `marks 80 >= id 6 < AND` |

This lab implements **Dijkstra's Shunting-Yard Algorithm** to perform this conversion and then evaluates the RPN expression to filter database records.

---

## 2. Operator Precedence Table

The parser implements the following precedence rules (higher number indicates higher priority):

| Operator | Precedence | Type | Description |
| :--- | :---: | :--- | :--- |
| `>`, `<`, `>=`, `<=`, `=`, `!=` | 3 | Comparison | Evaluated first |
| `AND` | 2 | Logical AND | Evaluated second |
| `OR` | 1 | Logical OR | Evaluated last |

---

## 3. Algorithm Phases

The program is structured into three distinct pipeline phases:

### Phase 1: Lexical Analysis (Tokenization)
The raw query string is scanned from left to right and broken down into individual tokens (`Identifier`, `Number`, `Operator`, `Keyword`, or `Parenthesis`). All whitespace is discarded, and keywords like `AND` and `OR` are normalized to uppercase for case-insensitivity.

### Phase 2: Shunting-Yard Algorithm
An operator stack and an output queue are used to convert the infix token list to postfix:
* **Operands** (columns/numbers) are pushed directly to the output.
* **Left Parenthesis `(`** is pushed to the operator stack.
* **Right Parenthesis `)`** pops operators from the stack to the output until a `(` is encountered.
* **Operators/Keywords** pop operators of greater or equal precedence from the stack to the output before pushing themselves.
* Finally, all remaining operators are popped to the output.

### Phase 3: Stack-Based Postfix Evaluator
The postfix expression is evaluated against each database row using an evaluation stack:
* **Operands** (numbers or column values retrieved from the row) are pushed to the stack.
* **Operators/Keywords** pop the top two values, apply the operation, and push the boolean result (`1` or `0`) back to the stack.
* The final remaining value on the stack determines whether the row matches the filter.

---

## 4. Code Structure

* **`main.cpp`**: Contains the complete implementation including the tokenizer, shunting-yard algorithm, stack evaluator, a hardcoded set of student records, and an interactive CLI.
* **`makefile`**: Automates compilation and testing.

---

## 5. Build and Run Instructions

To compile and run the program, use the provided `makefile`:

```bash
# Compile the program
make

# Run the program
make run

# Clean build artifacts
make clean
```

### Alternatively (Manual Compilation)
```bash
g++ -std=c++17 -Wall -Wextra -O2 -o main main.cpp
./main
```

---

## 6. Example Execution Flow

### Input Query:
`id > 2 AND (marks >= 80 OR age < 20)`

1. **Tokens Produced:**
   `[id] [>] [2] [AND] [(] [marks] [>=] [80] [OR] [age] [<] [20] [)]`

2. **Postfix Output (RPN):**
   `id 2 > marks 80 >= age 20 < OR AND`

3. **Evaluation Trace (Row by Row):**
   * Student `Aarav` (id=1, marks=78, age=19) $\rightarrow$ `1 > 2` (False) $\rightarrow$ Excluded.
   * Student `Diya` (id=2, marks=91, age=20) $\rightarrow$ `2 > 2` (False) $\rightarrow$ Excluded.
   * Student `Meera` (id=4, marks=88, age=22) $\rightarrow$ `4 > 2` (True), `88 >= 80` (True) $\rightarrow$ **Matched!**
