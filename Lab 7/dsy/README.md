# Dijkstra's Shunting-Yard Algorithm

Converts a SQL `WHERE` clause from infix to postfix (RPN) and evaluates it against a set of rows.

---

## The Problem

When you write `id > 3 AND age < 25 OR age >= 30`, the database needs to know which operators bind tighter. Without knowing that `AND` has higher precedence than `OR`, the result is ambiguous.

Same issue as `2 + 3 * 4` — should it be `14` or `20`?

---

## Infix vs Postfix

**Infix** — operators sit between operands, needs precedence rules and parentheses:
```
id > 3 AND (age < 25 OR age >= 30)
```

**Postfix (RPN)** — operators come after their operands, no parentheses needed:
```
id 3 > age 25 < age 30 >= OR AND
```

Postfix can be evaluated in a single left-to-right pass with a stack — just push operands, and when you hit an operator, pop two, apply, push the result.

---

## Operator Precedence

| Operator | Precedence |
|---|---|
| `>` `<` `>=` `<=` `=` | 3 (highest) |
| `AND` | 2 |
| `OR` | 1 (lowest) |

All are left-associative.

---

## The Algorithm

Two containers:
- **output queue** — builds the final postfix expression
- **operator stack** — holds operators temporarily

Walk the tokens left to right:

| Token | Action |
|---|---|
| operand | push to output |
| operator `o1` | pop operators with precedence ≥ `o1` to output, then push `o1` |
| `(` | push to stack |
| `)` | pop to output until `(`, discard the `(` |

Drain remaining operators onto output at the end.

---

## Worked Example

Input: `id > 3 AND (age < 25 OR age >= 30)`

| Token | Output | Stack |
|---|---|---|
| `id` | id | |
| `>` | id | > |
| `3` | id 3 | > |
| `AND` | id 3 > | AND |
| `(` | id 3 > | AND ( |
| `age` | id 3 > age | AND ( |
| `<` | id 3 > age | AND ( < |
| `25` | id 3 > age 25 | AND ( < |
| `OR` | id 3 > age 25 < | AND ( OR |
| `age` | id 3 > age 25 < age | AND ( OR |
| `>=` | id 3 > age 25 < age | AND ( OR >= |
| `30` | id 3 > age 25 < age 30 | AND ( OR >= |
| `)` | id 3 > age 25 < age 30 >= OR | AND |
| end | id 3 > age 25 < age 30 >= OR AND | — |

Result: `id 3 > age 25 < age 30 >= OR AND`

---

## Build & Run

```bash
g++ -std=c++17 main.cpp -o main && ./main
```
