# Lab 7 — Shunting-Yard Evaluator + Minimal SQL SELECT Parser

**Student:** Romit Raj Sahu | **Roll:** 24BCS10436

---

## 1. Dijkstra's Shunting-Yard Algorithm

### Motivation

Humans write arithmetic in **infix** notation: `3 + 4 * 2`. Computers evaluate expressions most simply in **postfix** (Reverse Polish Notation): `3 4 2 * +`. The Shunting-Yard algorithm converts infix to postfix using two structures:

- **Output queue** — holds numbers and operators in postfix order
- **Operator stack** — temporarily holds operators and left parentheses

### Operator Precedence Table

| Operator | Precedence | Associativity |
|----------|-----------|--------------|
| `^` | 4 | Right |
| `*` `/` | 3 | Left |
| `+` `-` | 2 | Left |

### Right-Associativity for `^`

`2 ^ 3 ^ 2` should parse as `2 ^ (3 ^ 2) = 512`, not `(2 ^ 3) ^ 2 = 64`. When the incoming operator has the same precedence as the stack top and is **right-associative**, we do NOT pop the stack top first — we push the new operator on top.

---

## 2. Step-by-Step Trace: `"3 + 4 * 2"`

| Step | Token | Output queue | Operator stack | Action |
|------|-------|-------------|---------------|--------|
| 1 | `3` | `[3]` | `[]` | Number → output |
| 2 | `+` | `[3]` | `[+]` | Operator; stack empty → push |
| 3 | `4` | `[3, 4]` | `[+]` | Number → output |
| 4 | `*` | `[3, 4]` | `[+, *]` | `*` has higher prec than `+` → push |
| 5 | `2` | `[3, 4, 2]` | `[+, *]` | Number → output |
| End | — | `[3, 4, 2, *, +]` | `[]` | Drain stack to output |

**Postfix evaluation of `3 4 2 * +`:**
- Push 3, push 4, push 2
- `*`: pop 2 and 4 → push 8
- `+`: pop 8 and 3 → push **11** ✓

---

## 3. Why Postfix is Easier to Evaluate than Infix

Infix requires tracking operator precedence, associativity, and parentheses simultaneously. Postfix (RPN) evaluation needs only a single stack and one pass:

- Token is a number → push
- Token is an operator → pop two operands, apply operator, push result

No precedence checking needed at evaluation time — it was already encoded into the postfix order during Shunting-Yard conversion.

---

## 4. SQL Parser Design

### Tokenizer Stages

1. **Keyword extraction** — locate `SELECT`, `FROM`, `WHERE` by uppercase string search
2. **Column list parsing** — extract and split on `,` (or detect `*`)
3. **Table name extraction** — single word between `FROM` and `WHERE`/end
4. **WHERE clause isolation** — raw substring after `WHERE`, preserved in original case

### Query Struct

```cpp
struct Query {
    vector<string> select_cols;  // empty = SELECT *
    string from_table;
    string where_clause;
};
```

### Row Filtering Logic

`execute()` iterates all rows. For each row:
1. If WHERE clause is non-empty, call `eval_where(row, where_clause)`
2. If false, skip the row
3. Otherwise, project: keep all columns (SELECT *) or only named columns
4. Append projected row to result

---

## 5. WHERE Clause Evaluation

**AND/OR splitting:**
- Split first on ` OR ` (lower precedence) — produces OR-groups
- Within each OR-group, split on ` AND ` — all must be true
- If any OR-group is fully true, the WHERE evaluates to true

**Condition parsing** (`eval_condition`):
- Detect operator: `<=`, `>=`, `!=`, `<`, `>`, `=` (checked in this order to avoid `<` matching `<=`)
- Extract column name (left) and value (right)
- If value is quoted (`'Alice'`) → lexicographic string comparison
- If value is numeric or an arithmetic expression → evaluate with `ShuntingYard`, compare numerically

---

## 6. ShuntingYard Integration with SQLParser

The `SQLParser` holds a `ShuntingYard` member. When `eval_condition` parses a numeric comparison like `age > 2 * 10 + 1`:
1. RHS = `"2 * 10 + 1"` is passed to `ShuntingYard::evaluate()`
2. Result = `21.0`
3. LHS = `std::stod(row["age"])`
4. Comparison: `lhs > 21.0`

This lets the WHERE clause contain arbitrary arithmetic on the right-hand side, not just literals.

---

## 7. Limitations

This is a teaching implementation with intentional scope limits:

- **Single table only** — no JOIN support
- **No aggregates** — no `COUNT`, `SUM`, `AVG`, `GROUP BY`
- **No subqueries** — the WHERE clause must be a flat boolean expression
- **No ORDER BY or LIMIT**
- **AND/OR parsing** is split-based — deeply nested parenthesized logic is not supported
- **Column name case** is preserved from the original SQL; the table's column keys must match exactly
