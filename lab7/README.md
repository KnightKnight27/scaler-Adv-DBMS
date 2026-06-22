# Lab 7 — Shunting-Yard Expression Evaluator + Minimal SQL `SELECT`

**Vimal Kumar Yadav · 24BCS10273**

Two small C++17 engines that share one idea — *parse a flat token stream into a
structure you can evaluate*:

1. **Dijkstra's Shunting-Yard expression evaluator** — converts an infix
   arithmetic expression to Reverse Polish Notation (RPN) and evaluates it.
2. **A minimal SQL `SELECT` engine** — lexes and parses
   `SELECT … FROM … WHERE …` and runs it over an in-memory `vector<Row>`,
   producing the filtered, projected result.

The code is deliberately split into small, single-purpose units to demonstrate
the **SOLID** principles (mapped out below).

---

## Build & run

Requires a C++17 compiler and CMake ≥ 3.16.

```bash
cd lab7
cmake -S . -B build
cmake --build build
./build/lab7            # Windows: .\build\Release\lab7.exe
```

Or compile directly:

```bash
# from lab7/
g++ -std=c++17 -Iinclude src/*.cpp src/expr/*.cpp src/sql/*.cpp -o lab7
```

(On Windows with MSVC: `cl /std:c++17 /EHsc /Iinclude src\*.cpp src\expr\*.cpp src\sql\*.cpp`.)

---

## Sample output

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

## Part 1 — Shunting-Yard

### Pipeline

```
"3 + 4 * 2"  ──Tokenizer──▶  [3] [+] [4] [*] [2]
             ──ShuntingYard──▶  3 4 2 * +        (RPN)
             ──RpnEvaluator──▶  11.0
```

### Supported grammar
- Numbers: integers and decimals (`3`, `3.14`, `.5`).
- Binary operators: `+ - * / %` and `^` (power).
- Parentheses for grouping.
- Prefix (unary) `+` / `-`, detected at the start of the expression, after
  another operator, or after `(`.

### Precedence & associativity

| Operator        | Precedence | Associativity |
| --------------- | ---------- | ------------- |
| unary `+` `-`   | 5          | right         |
| `^`             | 4          | right         |
| `*` `/` `%`     | 3          | left          |
| `+` `-`         | 2          | left          |

Notes:
- `^` is **right-associative**, so `2 ^ 3 ^ 2 = 2 ^ (3 ^ 2) = 512`.
- Unary operators **bind tightest**, so `-2 ^ 2 = (-2) ^ 2 = 4`. (This is an
  explicit design choice; choosing the other convention is a one-line change in
  `OperatorTable`.)
- Division/modulo by zero and malformed input throw `std::runtime_error`.

---

## Part 2 — Minimal SQL `SELECT`

### Pipeline

```
SQL text ──SqlLexer──▶ tokens ──SqlParser──▶ SelectStatement (+ WHERE tree)
                                  └──QueryEngine.execute(stmt, Table)──▶ Table
```

### Supported grammar

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

- Keywords are case-insensitive (`SELECT`, `select`, `Select`).
- Values are numbers or single-quoted strings; comparing a number against a
  string is a reported type error.
- `WHERE` supports `AND`/`OR` with `OR` lowest precedence, parentheses, and
  short-circuit evaluation.
- Rows are stored as `vector<Row>`, where each `Row` maps column → value.

---

## SOLID mapping

- **Single Responsibility** — each class does exactly one thing: `Tokenizer`
  lexes, `ShuntingYard` reorders, `RpnEvaluator` computes; `SqlLexer`,
  `SqlParser`, and `QueryEngine` likewise.
- **Open/Closed** — operator metadata lives in `OperatorTable` and operator
  semantics in lookup tables of callables in `RpnEvaluator`; a new operator is a
  registration, not a change to the algorithms. New WHERE node types extend
  `BoolExpr` without touching `QueryEngine`.
- **Liskov Substitution** — every `BoolExpr` subtype (`Comparison`, `Logical`)
  is usable anywhere a `BoolExpr` is expected; the engine never downcasts.
- **Interface Segregation** — the expression pipeline is split into three
  focused contracts (`ITokenizer`, `IInfixToPostfix`, `IRpnEvaluator`) instead
  of one fat interface.
- **Dependency Inversion** — `ExpressionEvaluator` depends on the interfaces and
  accepts implementations via constructor injection; `QueryEngine` depends on
  the `BoolExpr` abstraction, not concrete predicate classes.

---

## Layout

```
lab7/
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
