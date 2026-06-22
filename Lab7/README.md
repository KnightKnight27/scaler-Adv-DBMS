# Lab 7 — Expression Parsing & a Minimal SQL SELECT Engine (C++)

- **Role Number:** 24BCS10345
- **Name:** Ansh Mahajan
- **Date:** 2026-06-22

## Aim

1. Implement **Dijkstra's Shunting Yard algorithm** to convert infix arithmetic
   expressions to postfix and evaluate them with a stack.
2. Implement a **minimal SQL `SELECT` parser** that runs queries against a
   collection of rows held in memory (vectors of structs).

Both parts share the same lesson: a stack (or a small hand-written parser) is
enough to turn flat text into something a machine can evaluate, and operator
precedence / grammar rules decide *when* each piece of work happens.

---

## Part 1 — Shunting Yard (`shunting_yard.cpp`)

### How it works

- **Tokenize** the string into numbers, operators and parentheses. A `-` (or
  `+`) is detected as *unary* when it appears at the start of the expression or
  right after another operator or `(`; unary minus becomes the internal
  operator `m`.
- **`toPostfix`** is the core of the algorithm: numbers are emitted straight to
  the output; an incoming operator first pops every stacked operator that binds
  at least as tightly (strictly tighter for a right-associative operator) before
  being pushed; `(` is pushed and `)` flushes operators back to the matching
  `(`.
- **`evaluatePostfix`** walks the postfix list pushing numbers and applying each
  operator to the top of the value stack.

Precedence used: `^` (4, right-assoc) > unary `-` (3) > `* /` (2) > `+ -` (1).

### Sample run

```text
infix:   3 + 4 * 2          postfix: 3 4 2 * +        result: 11
infix:   (3 + 4) * 2        postfix: 3 4 + 2 *        result: 14
infix:   10 - 2 - 3         postfix: 10 2 - 3 -       result: 5     (left assoc)
infix:   2 ^ 3 ^ 2          postfix: 2 3 2 ^ ^        result: 512   (right assoc)
infix:   4 * -2             postfix: 4 2 u- *         result: -8    (unary minus)
infix:   -(5 - 8)           postfix: 5 8 - u-         result: 3
infix:   100 / (2 + 3) / 2  postfix: 100 2 3 + / 2 /  result: 10
infix:   7 + (6 * 5 + 1     error:   mismatched parentheses
```

The right-associative `^` (`2^3^2 = 512`, not `64`) and the left-associative
`-` (`10-2-3 = 5`) confirm associativity is handled correctly, and the last
case shows the parenthesis-balancing check firing.

---

## Part 2 — SQL SELECT engine (`sql_parser.cpp`)

### Data

Five `Row{ id, name, age }` records live in a `std::vector` in memory — the
"table" `people`:

```text
101 Ansh 20 | 102 Riya 22 | 103 Karan 19 | 104 Sneha 23 | 105 Vikram 21
```

### Grammar supported

```text
SELECT  <* | col[, col ...]>
FROM    people
[WHERE  <col op literal> [AND <col op literal> ...]]
[ORDER BY <col> [ASC | DESC]]

op := =  !=  <  <=  >  >=
```

Keywords are case-insensitive, string literals are single-quoted, numeric
columns (`id`, `age`) compare numerically and `name` compares lexicographically.
The flow is: a hand-written **tokenizer** → a small **recursive-descent parser**
that builds a column list, a list of `Condition`s and an optional sort key →
an **executor** that filters the vector, optionally `stable_sort`s it, and
projects the requested columns.

### Sample run

```text
SQL> SELECT name, age FROM people WHERE age >= 21
  name |  age
  Riya | 22
  Sneha | 23
  Vikram | 21
  (3 row(s))

SQL> SELECT * FROM people WHERE age > 20 ORDER BY age DESC
  104 | Sneha | 23
  102 | Riya | 22
  105 | Vikram | 21

SQL> SELECT id, name FROM people WHERE age >= 20 AND id < 104
  101 | Ansh
  102 | Riya

SQL> SELECT salary FROM people
  error: unknown column: salary
```

`WHERE ... AND ...`, `ORDER BY ... DESC`, case-insensitive keywords and the
unknown-column error path all behave as expected.

---

## Build and run

```bash
# Direct (as specified)
g++ -std=c++17 shunting_yard.cpp -o shunting
g++ -std=c++17 sql_parser.cpp   -o parser
./shunting
./parser

# Or with CMake (builds both)
cmake -S . -B build && cmake --build build
./build/shunting
./build/parser
```

Both programs run their built-in sample set first, then evaluate any extra
lines piped in on stdin (expressions for `shunting`, queries for `parser`).

## Files

| File | Purpose |
| --- | --- |
| [shunting_yard.cpp](shunting_yard.cpp) | Infix → postfix conversion + evaluation |
| [sql_parser.cpp](sql_parser.cpp) | Tokenizer + recursive-descent SELECT engine |
| [CMakeLists.txt](CMakeLists.txt) | Builds both executables (C++17, warnings on) |
| `README.md` | This write-up |
