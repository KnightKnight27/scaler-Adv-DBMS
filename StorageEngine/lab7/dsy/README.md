# Dijkstra's Shunting-Yard — Learning Notes

**Name.** Aman Yadav
**Roll No.** 24BCS10183
**Class.** B (2nd year)

This is where I jot down what I learn while writing the code in `main.cpp`.

> I started this not knowing what infix/postfix even were, or why we'd bother
> converting a `WHERE` clause at all. So this README starts from zero.

---

## 0. Wait — what problem are we even solving?

When you write `2 + 3 * 4`, you *know* the answer is `14`, not `20`. Why?
Because of **precedence**: `*` happens before `+`. Your brain silently applied
a rule.

A computer reading the raw text `2 + 3 * 4` left-to-right has **no idea** about
that rule. If it just went token by token it would do `2 + 3 = 5`, then
`5 * 4 = 20` — wrong.

So any program that evaluates expressions (a calculator, a compiler, **or a
database evaluating a `WHERE` clause**) needs a way to respect precedence and
parentheses. The shunting-yard algorithm is one famous way to do exactly that.

The same problem shows up in SQL:

```sql
WHERE id > 3 AND age < 25 OR age >= 30
```

Does this mean `(id > 3 AND age < 25) OR age >= 30`, or
`id > 3 AND (age < 25 OR age >= 30)`? They give different results! The engine
needs precedence rules (`AND` before `OR`) to decide — exactly like `*` before `+`.

---

## 1. What is infix? What is postfix?

These are just **two ways of writing the same expression**.

### Infix (the normal, human way)

The operator sits **between** the operands:

```
2 + 3
id > 5
```

Problem: infix is *ambiguous on its own*. To read `2 + 3 * 4` correctly you
need extra rules (precedence) and sometimes parentheses. That's work for a
computer.

### Postfix (a.k.a. RPN — Reverse Polish Notation, the machine-friendly way)

The operator comes **after** its operands:

```
Infix:   2 + 3 * 4
Postfix: 2 3 4 * +
```

The magic of postfix: **there are no parentheses and no precedence rules to
remember.** The order of the tokens already tells you what to do first. A
computer can evaluate it in one left-to-right pass using a single stack:

```
Read: 2   → push 2            stack: [2]
Read: 3   → push 3            stack: [2, 3]
Read: 4   → push 4            stack: [2, 3, 4]
Read: *   → pop 4 and 3, push 3*4=12   stack: [2, 12]
Read: +   → pop 12 and 2, push 2+12=14 stack: [14]
Answer: 14  ✅
```

That's why we convert: **infix is easy for humans to write, postfix is easy for
machines to evaluate.** Shunting-yard is the bridge between the two.

---

## 2. "But my query parser already works — why do I need this?"

Good question, and the honest answer is: **you don't strictly need it — both
approaches solve the same problem, just differently.**

In `queryParsing/main.cpp` I wrote a **recursive-descent parser** that builds
an **AST (tree)**. Precedence there is baked into *which function calls which*
(`parseExpression` handles `OR`, `parsePrimary` handles parentheses, etc.). When
I evaluate, I walk the tree.

Shunting-yard is a **different classic technique** for the same job. Instead of
a tree, it produces a **flat postfix list** that you evaluate with a stack.

| | Query parser (`queryParsing/`) | Shunting-yard (`dsy/`) |
|---|---|---|
| Output shape | Tree (AST) | Flat list (postfix / RPN) |
| Handles precedence via | grammar structure (function call order) | an operator stack + precedence table |
| Evaluate by | walking the tree recursively | one left-to-right pass with a stack |
| Where you see it | most real compilers & DB planners | calculators, expression evaluators |

So nothing is "wrong" with the simple parser — it's actually how most real
databases build query plans (trees). The lab makes us implement shunting-yard
because it's a **fundamental algorithm** worth knowing, and it shows a
*second, completely different way* to handle operator precedence. Same problem,
two tools.

---

## 3. Operator precedence (the rules this algorithm enforces)

Higher precedence = binds tighter = evaluated first.

| Operator        | Precedence |
|-----------------|------------|
| `>` `<` `>=` `<=` `=` (comparison) | 3 (highest) |
| `AND`           | 2 |
| `OR`            | 1 (lowest) |

So `id > 3 AND age < 25` means `(id > 3) AND (age < 25)` — the comparisons run
before the `AND`. And `AND` runs before `OR`. All are **left-associative**
(left-to-right when precedence ties).

---

## 4. The Algorithm

Two containers:

- an **output queue** — builds the final postfix expression
- an **operator stack** — temporarily holds operators / `(` waiting for their operands

Walk the tokens left to right:

| Token | Action |
|-------|--------|
| **operand** (column name or number) | push straight onto the **output** |
| **operator** `o1` | while the stack top is an operator with precedence ≥ `o1`, pop it to output; then push `o1` |
| **`(`** | push onto the stack |
| **`)`** | pop operators to output until a `(` is found, then discard the `(` |

At the end, pop any remaining operators onto the output.

```
Tokens
   ↓
[ output queue ]   ← operands + popped operators
[ operator stack]  ← operators waiting / parentheses
   ↓
Postfix (RPN)
```

---

## 5. Worked example (SQL WHERE clause)

Input: `id > 3 AND (age < 25 OR age >= 30)`

| Token | Output (queue)              | Stack         |
|-------|-----------------------------|---------------|
| `id`  | id                          |               |
| `>`   | id                          | >             |
| `3`   | id 3                        | >             |
| `AND` | id 3 >                      | AND           |
| `(`   | id 3 >                      | AND (         |
| `age` | id 3 > age                  | AND (         |
| `<`   | id 3 > age                  | AND ( <       |
| `25`  | id 3 > age 25               | AND ( <       |
| `OR`  | id 3 > age 25 <             | AND ( OR      |
| `age` | id 3 > age 25 < age         | AND ( OR      |
| `>=`  | id 3 > age 25 < age         | AND ( OR >=   |
| `30`  | id 3 > age 25 < age 30      | AND ( OR >=   |
| `)`   | id 3 > age 25 < age 30 >= OR| AND           |
| (end) | id 3 > age 25 < age 30 >= OR AND | —        |

Result: `id 3 > age 25 < age 30 >= OR AND`

---

## 6. Evaluating the postfix

Walk the postfix left to right with a stack:

- **operand** → push its value (a number, or the row's column value)
- **operator** → pop the top two, apply, push the result (comparisons push `0`/`1`)

The single value left on the stack at the end is the answer (`true`/`false`)
for that row. `main.cpp` runs this over a list of employees and prints the rows
that match.

---

## 7. Build & run

```bash
g++ -std=c++17 main.cpp -o main && ./main
```

Expected output:

```
Infix WHERE:  id > 3 AND (age < 25 OR age >= 30)
Postfix (RPN): id 3 > age 25 < age 30 >= OR AND

Rows matching the WHERE clause:
  Sneha (id=4, age=21)
  Vivaan (id=5, age=20)
  Ishaan (id=6, age=31)
  Meera (id=7, age=22)
```
