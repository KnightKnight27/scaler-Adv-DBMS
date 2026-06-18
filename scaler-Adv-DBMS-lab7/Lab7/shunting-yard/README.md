# Lab 7 — Shunting-Yard (Dijkstra)

**24BCS10404 — Rajveer Bishnoi**

---

## What's the problem?

`2 + 3 * 4` = 14, not 20 — because `*` runs before `+`. A plain left-to-right reader gets it wrong. Anything that evaluates expressions (calculator, compiler, **database WHERE clause**) needs a way to honour precedence and parentheses. Shunting-yard is the classic recipe.

In SQL:

```sql
WHERE id > 3 AND age < 25 OR age >= 30
```

Is that `(id > 3 AND age < 25) OR age >= 30` or `id > 3 AND (age < 25 OR age >= 30)`? Different answers. The engine uses precedence (`AND` before `OR`) to pick — same idea as `*` before `+`.

---

## Infix vs Postfix

| | Infix | Postfix (RPN) |
|---|---|---|
| How | operator between operands | operator after operands |
| Example | `2 + 3 * 4` | `2 3 4 * +` |
| Needs precedence rules? | Yes | No |
| Evaluation | complex | one left-to-right stack pass |

Postfix evaluation:

```
2  → push           [2]
3  → push           [2, 3]
4  → push           [2, 3, 4]
*  → pop 4,3 push 12 [2, 12]
+  → pop 12,2 push 14 [14]
→ 14  ✅
```

---

## Precedence table

| Operator | Rank |
|----------|------|
| `>` `<` `>=` `<=` `=` | 3 (highest) |
| `AND` | 2 |
| `OR`  | 1 (lowest) |

All left-associative.

---

## The algorithm

Two containers: an **output queue** (assembles postfix) and an **operator stack** (parks operators until their operands arrive).

| Token | Action |
|-------|--------|
| operand | append to output |
| operator `o1` | pop operators of rank ≥ `o1` to output, then push `o1` |
| `(` | push to stack |
| `)` | pop to output until `(`, drop the `(` |

Flush remaining operators to output at the end.

---

## Worked example

Input: `id > 3 AND (age < 25 OR age >= 30)`

| Token | Output | Stack |
|-------|--------|-------|
| `id`  | id | |
| `>`   | id | > |
| `3`   | id 3 | > |
| `AND` | id 3 > | AND |
| `(`   | id 3 > | AND ( |
| `age` | id 3 > age | AND ( |
| `<`   | id 3 > age | AND ( < |
| `25`  | id 3 > age 25 | AND ( < |
| `OR`  | id 3 > age 25 < | AND ( OR |
| `age` | id 3 > age 25 < age | AND ( OR |
| `>=`  | id 3 > age 25 < age | AND ( OR >= |
| `30`  | id 3 > age 25 < age 30 | AND ( OR >= |
| `)`   | id 3 > age 25 < age 30 >= OR | AND |
| end   | id 3 > age 25 < age 30 >= OR AND | — |

Result: `id 3 > age 25 < age 30 >= OR AND`

---

## Build & run

```bash
g++ -std=c++17 main.cpp -o main && ./main
```

---

## Files

- `main.cpp` — shunting-yard converter + postfix evaluator
