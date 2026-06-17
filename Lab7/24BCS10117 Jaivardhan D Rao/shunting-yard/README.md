# Shunting-Yard (Dijkstra)

Scratch notes I kept while building `main.cpp`. Written for my future self, so
it starts from the very basics.

---

## 0. What's the actual problem?

Take `2 + 3 * 4`. You instantly say `14`, not `20`, because `*` runs before `+`.
That rule lives in your head — a plain left-to-right reader doesn't have it and
would compute `2 + 3 = 5` then `5 * 4 = 20`, which is wrong.

So anything that evaluates expressions — a calculator, a compiler, **a database
chewing through a `WHERE` clause** — needs a way to honour precedence and
parentheses. Shunting-yard is one well-known recipe for that.

In SQL it shows up immediately:

```sql
WHERE id > 3 AND age < 25 OR age >= 30
```

Is that `(id > 3 AND age < 25) OR age >= 30` or
`id > 3 AND (age < 25 OR age >= 30)`? Different answers. The engine leans on
precedence (`AND` before `OR`) to pick — same idea as `*` before `+`.

---

## 1. Infix vs postfix

Two notations for the **same** expression.

### Infix (how people write it)

Operator sits **between** the operands:

```
2 + 3
id > 5
```

Downside: infix can't be read unambiguously on its own — you need precedence
rules and sometimes parentheses. Extra work for a machine.

### Postfix (RPN — machine-friendly)

Operator comes **after** its operands:

```
Infix:   2 + 3 * 4
Postfix: 2 3 4 * +
```

The win: **no parentheses, no precedence rules to track.** Token order alone
says what runs first, so one left-to-right pass over a single stack evaluates it:

```
2  → push           [2]
3  → push           [2, 3]
4  → push           [2, 3, 4]
*  → pop 4,3 push 12 [2, 12]
+  → pop 12,2 push 14 [14]
→ 14  ✅
```

That's the reason for converting: **infix is comfortable to write, postfix is
trivial to evaluate.** Shunting-yard bridges the two.

---

## 2. "My parser already works — why this too?"

Honest answer: you don't strictly need it. Both approaches solve the same
problem in different shapes.

In `queryParsing/main.cpp` I wrote a **recursive-descent parser** that builds a
**tree (AST)**. Precedence there is encoded in *which function calls which*.
Evaluation walks the tree.

Shunting-yard is the other classic technique: it emits a **flat postfix list**
you evaluate with a stack instead.

| | Recursive-descent (`queryParsing/`) | Shunting-yard (`dsy/`) |
|---|---|---|
| Output | Tree (AST) | Flat list (RPN) |
| Precedence from | grammar / call order | operator stack + rank table |
| Evaluated by | recursive tree walk | single left-to-right stack pass |
| Seen in | compilers, DB planners | calculators, expr evaluators |

Neither is "wrong" — real databases lean on trees. The lab uses shunting-yard
because it's a foundational algorithm and a genuinely different second angle on
precedence. One problem, two tools.

---

## 3. Precedence table (what this code enforces)

Higher = binds tighter = runs first.

| Operator | Rank |
|----------|------|
| `>` `<` `>=` `<=` `=` | 3 (highest) |
| `AND` | 2 |
| `OR` | 1 (lowest) |

So `id > 3 AND age < 25` is `(id > 3) AND (age < 25)`, and `AND` beats `OR`. All
left-associative (left-to-right on ties).

---

## 4. The algorithm

Two containers:

- an **output queue** — assembles the final postfix
- an **operator stack** — parks operators / `(` until their operands arrive

Scan tokens left to right:

| Token | Do |
|-------|----|
| **operand** (column / number) | append to **output** |
| **operator** `o1` | while stack top is an operator of rank ≥ `o1`, pop it to output; then push `o1` |
| **`(`** | push to stack |
| **`)`** | pop to output until `(`, then drop the `(` |

At the end, flush remaining operators to output.

```
Tokens
   ↓
[ output queue ]   ← operands + popped operators
[ operator stack]  ← waiting operators / parens
   ↓
Postfix (RPN)
```

---

## 5. Worked example

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

## 6. Evaluating the postfix

Scan postfix left to right with a stack:

- **operand** → push its value (a number, or the row's column value)
- **operator** → pop the top two, apply, push the result (comparisons push `0`/`1`)

The lone value left at the end is that row's `true`/`false`. `main.cpp` runs
this across a staff list and prints the matching rows.

---

## 7. Build & run

```bash
g++ -std=c++17 main.cpp -o main && ./main
```
