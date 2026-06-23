# Lab 7 — Shunting-Yard Expression Evaluator + Minimal SQL SELECT
**Name:** Tirth Shah
**Roll Number:** 24BCS10347

## Problem Statement
Build a tiny in-memory query engine in C++17 that can execute
`SELECT <col, ... | *> FROM <table> [WHERE <expr>]` over a generic table
(`std::vector<Row>`, where each `Row` maps a column name to a `Value` that is
either an integer or a string). The **WHERE clause is evaluated using Dijkstra's
Shunting-Yard algorithm**: the infix predicate is tokenised, converted to
Reverse Polish Notation (RPN / postfix) honouring a precedence and
associativity table plus parentheses, and then evaluated per row with a value
stack. Arithmetic, all six comparison operators, and boolean AND / OR / NOT are
supported, including string equality / inequality.

## Precedence & Associativity Table
Higher precedence binds tighter (is applied first).

| Level | Operators            | Kind        | Associativity |
|-------|----------------------|-------------|---------------|
| 6     | `NOT`                | unary logic | right         |
| 5     | `*`  `/`             | arithmetic  | left          |
| 4     | `+`  `-`             | arithmetic  | left          |
| 3     | `>` `<` `>=` `<=` `=` `!=` | comparison | left      |
| 2     | `AND`                | logic       | left          |
| 1     | `OR`                 | logic       | left (lowest) |

Notes:
- `NOT` is unary and right-associative, with the highest precedence among the
  logical operators. Because it outranks comparisons, `NOT age >= 30` parses as
  `(NOT age) >= 30`; to negate a whole comparison, write `NOT (age >= 30)`.
- Parentheses `( )` override precedence as usual.

## How Shunting-Yard Works (short)
Scan the infix tokens left to right, maintaining an **operator stack** and an
**output queue** (the RPN result):
- **Operand** (column / number / string) → append to the output queue.
- **`(`** → push onto the operator stack.
- **`)`** → pop operators to the output until the matching `(` is found, then
  discard the `(`.
- **Operator `o1`** → while an operator `o2` is on top of the stack with either
  higher precedence, or equal precedence and `o1` is left-associative, pop `o2`
  to the output; then push `o1`.
- **End of input** → pop any remaining operators to the output.

The resulting RPN is evaluated with a **value stack**: operands are pushed
(columns resolved against the current `Row`); each operator pops its operands,
computes a result (arithmetic → number, comparison/logic → 0/1 boolean), and
pushes it back. The single remaining stack value is the result.

## Architecture / Flow
```
SQL string
   │  Lexer (lexer.cpp)        — tokenise: keywords, ids, numbers, 'strings',
   ▼                             = != > < >= <=, + - * /, ( ) , *, AND/OR/NOT
tokens
   │  Parser (sql.cpp)         — recursive-descent skeleton:
   ▼                             SELECT <proj> FROM <table> [WHERE <infix...>]
SelectStatement (projection + table name + raw infix WHERE tokens)
   │  toRPN (shunting_yard.cpp)— Dijkstra's Shunting-Yard: infix → RPN  (once)
   ▼
RPN token list
   │  executeSelect (sql.cpp)  — for each Row: evalRPN(rpn, row) → keep if true,
   ▼                             then project requested columns
Result Table  → printResult
```
Files: `value.h` (Value/Row/Table), `lexer.h/.cpp`, `shunting_yard.h/.cpp`
(the graded core), `sql.h/.cpp` (parser + executor), `main.cpp` (demo + tests).

## Worked Example (infix → RPN)
Input: `age > 18 AND (dept = 'eng' OR id < 3)`

| Token        | Action                                              | Output queue                     | Op stack        |
|--------------|-----------------------------------------------------|----------------------------------|-----------------|
| `age`        | operand → output                                    | `age`                            |                 |
| `>`          | push (stack empty)                                  | `age`                            | `>`             |
| `18`         | operand → output                                    | `age 18`                         | `>`             |
| `AND`        | `>` (prec 3) > `AND` (prec 2) → pop `>`, push `AND` | `age 18 >`                       | `AND`           |
| `(`          | push                                                | `age 18 >`                       | `AND (`         |
| `dept`       | operand → output                                    | `age 18 > dept`                  | `AND (`         |
| `=`          | top is `(` → push                                   | `age 18 > dept`                  | `AND ( =`       |
| `'eng'`      | operand → output                                    | `age 18 > dept eng`              | `AND ( =`       |
| `OR`         | `=` (3) > `OR` (1) → pop `=`, push `OR`              | `age 18 > dept eng =`            | `AND ( OR`      |
| `id`         | operand → output                                    | `age 18 > dept eng = id`         | `AND ( OR`      |
| `<`          | top `OR` (1) < `<` (3) → push                        | `age 18 > dept eng = id`         | `AND ( OR <`    |
| `3`          | operand → output                                    | `age 18 > dept eng = id 3`       | `AND ( OR <`    |
| `)`          | pop to `(` : pop `<`, `OR`, discard `(`             | `age 18 > dept eng = id 3 < OR`  | `AND`           |
| *(end)*      | pop remaining: `AND`                                | `age 18 > dept eng = id 3 < OR AND` |              |

**RPN:** `age 18 > dept eng = id 3 < OR AND`

## Build & Run
Using Apple clang directly:
```
c++ -std=c++17 -Wall -Wextra *.cpp -o lab7 && ./lab7
```
Using CMake:
```
mkdir build && cd build
cmake ..
cmake --build .
./lab7
```

## Sample Output
```
============================================================
 Lab 7 - Shunting-Yard Expression Evaluator + SQL SELECT
============================================================

SQL> SELECT name FROM employees WHERE age > 18 AND (dept = 'eng' OR id < 3)
  name
  Alice
  Bob
  Carol
  (3 rows)

SQL> SELECT * FROM employees WHERE NOT (age >= 30)
  age   dept    id      name
  22    sales   2       Bob
  17    eng     4       Dave
  28    hr      5       Eve
  (3 rows)

SQL> SELECT name, age FROM employees WHERE age + 2 * 3 > 25
  age   name
  30    Alice
  22    Bob
  41    Carol
  28    Eve
  (4 rows)

SQL> SELECT * FROM numbers WHERE n / 2 >= 3 AND n != 12
  label n
  six   6
  twenty        20
  (2 rows)

------------------------------------------------------------
Running self-tests...
ALL TESTS PASSED
```
