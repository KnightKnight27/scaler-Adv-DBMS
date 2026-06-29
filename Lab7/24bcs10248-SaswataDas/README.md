# Lab 7 — Expression Evaluation via Shunting-Yard & a Minimal SQL SELECT Engine

**Saswata Das · 24BCS10248**

This project implements two interconnected C++17 engines built around the same
core concept: transforming a flat sequence of tokens into an evaluable structure.

- **Shunting-Yard Expression Evaluator** — Parses infix arithmetic into Reverse
  Polish Notation (RPN) using Dijkstra's algorithm, then evaluates the result.
- **SQL SELECT Engine** — Tokenizes, parses, and executes basic
  `SELECT … FROM … WHERE …` queries against an in-memory row store
  (`vector<Row>`), returning filtered and projected output.

Both components are decomposed into small, focused classes that follow SOLID
design principles (details below).

---

## Getting Started

**Prerequisites:** C++17-capable compiler, CMake 3.16+

```bash
cd Lab7/24bcs10248-SaswataDas
cmake -S . -B build
cmake --build build
./build/Lab7            # Windows: .\build\Release\Lab7.exe
```

Alternatively, compile without CMake:

```bash
# from Lab7/24bcs10248-SaswataDas/
g++ -std=c++17 -Iinclude src/*.cpp src/expr/*.cpp src/sql/*.cpp -o Lab7
```

(MSVC: `cl /std:c++17 /EHsc /Iinclude src\*.cpp src\expr\*.cpp src\sql\*.cpp`.)

---

## Example Output

```
=== Part 1: Shunting-Yard expression evaluator ===
3 + 4 * 2 / (1 - 5)    | RPN: 3 4 2 * 1 5 - / +    = 1
2 ^ 3 ^ 2              | RPN: 2 3 2 ^ ^            = 512
-2 ^ 2                 | RPN: 2 u- 2 ^             = 4
10 % 3 + 1             | RPN: 10 3 % 1 +           = 2
(1 + 2) * (3 + 4)      | RPN: 1 2 + 3 4 + *        = 21
-(3 + 4) * -2          | RPN: 3 4 + u- 2 u- *      = 14

=== Part 2: minimal SQL SELECT over vector<Row> ===
students:
  id | name  | gpa | age
  -- | ----- | --- | ---
  1  | Aarav | 9.1 | 20
  2  | Diya  | 7.4 | 22
  3  | Kabir | 8.6 | 19
  4  | Meera | 6.9 | 21
  5  | Rohan | 8   | 23

> SELECT name, gpa FROM students WHERE gpa >= 8.0 AND age < 23
  name  | gpa
  ----- | ---
  Aarav | 9.1
  Kabir | 8.6
(2 row(s))

> SELECT * FROM students WHERE age > 21 OR gpa >= 9.0
  ...
> SELECT name FROM students WHERE name >= 'K'
  ...
```

---

## Part 1 — Shunting-Yard Evaluator

### How It Works

The evaluator processes expressions through a three-stage pipeline:

```
"3 + 4 * 2"  ──Tokenizer──▶  [3] [+] [4] [*] [2]
             ──ShuntingYard──▶  3 4 2 * +        (RPN)
             ──RpnEvaluator──▶  11.0
```

### What It Handles

- **Numbers:** integers and decimals (`3`, `3.14`, `.5`)
- **Binary operators:** `+ - * / %` and `^` (exponentiation)
- **Parentheses** for grouping
- **Unary `+` / `-`** — recognized at the beginning of an expression, after
  another operator, or following an opening parenthesis

### Operator Precedence and Associativity

| Operator        | Precedence | Associativity |
| --------------- | ---------- | ------------- |
| unary `+` `-`   | 5          | right         |
| `^`             | 4          | right         |
| `*` `/` `%`     | 3          | left          |
| `+` `-`         | 2          | left          |

Key behaviors:
- Exponentiation is **right-associative**: `2 ^ 3 ^ 2` evaluates as `2 ^ (3 ^ 2) = 512`.
- Unary operators have **highest precedence**, so `-2 ^ 2` becomes `(-2) ^ 2 = 4`.
  (This convention can be flipped with a single change in `OperatorTable`.)
- Division or modulo by zero and malformed expressions raise `std::runtime_error`.

---

## Part 2 — SQL SELECT Engine

### Processing Pipeline

```
SQL text ──SqlLexer──▶ tokens ──SqlParser──▶ SelectStatement (+ WHERE tree)
                                  └──QueryEngine.execute(stmt, Table)──▶ Table
```

### Supported SQL Subset

```
select     := SELECT projection FROM identifier [ WHERE orExpr ]
projection := '*' | identifier ( ',' identifier )*
orExpr     := andExpr ( OR andExpr )*
andExpr    := primary ( AND primary )*
primary    := '(' orExpr ')' | comparison
comparison := identifier compOp operand
operand    := number | 'string'
compOp     := = | == | != | <> | < | <= | > | >=
```

- SQL keywords (`SELECT`, `FROM`, etc.) are **case-insensitive**.
- Literal values can be numbers or single-quoted strings; comparing mismatched
  types produces a type error.
- `WHERE` clauses support `AND` / `OR` (with `OR` at lower precedence),
  parenthesized sub-expressions, and short-circuit evaluation.
- Data is stored as `vector<Row>`, where each `Row` maps column names to values.

---

## SOLID Principles in Practice

- **Single Responsibility** — Every class has one job: `Tokenizer` handles
  lexing, `ShuntingYard` converts to RPN, `RpnEvaluator` performs arithmetic.
  On the SQL side, `SqlLexer`, `SqlParser`, and `QueryEngine` each own a
  distinct processing stage.
- **Open/Closed** — Operator definitions live in `OperatorTable` and evaluation
  logic uses lookup tables in `RpnEvaluator`. Adding a new operator means
  registering it, not modifying core algorithms. On the SQL side, new predicate
  types extend `BoolExpr` without changing `QueryEngine`.
- **Liskov Substitution** — All `BoolExpr` subtypes (`Comparison`, `Logical`)
  are interchangeable wherever a `BoolExpr` is expected; no downcasting occurs.
- **Interface Segregation** — The expression pipeline defines three narrow
  interfaces (`ITokenizer`, `IInfixToPostfix`, `IRpnEvaluator`) rather than one
  monolithic contract.
- **Dependency Inversion** — `ExpressionEvaluator` programs against interfaces
  and receives concrete implementations through constructor injection.
  `QueryEngine` depends on the `BoolExpr` abstraction, not specific predicate
  classes.

---

## Directory Structure

```
Lab7/24bcs10248-SaswataDas/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── expr/   Token, OperatorTable, Interfaces, Tokenizer,
│   │           ShuntingYard, RpnEvaluator, ExpressionEvaluator
│   └── sql/    Value, Row, Table, SqlToken, SqlLexer, Ast,
│               SqlParser, QueryEngine
└── src/
    ├── expr/   *.cpp
    ├── sql/    *.cpp
    └── main.cpp   demo driver
```
