# Lab 7 — Query Parsing

**24BCS10404 — Rajveer Bishnoi**

---

## 1. Tokenization (lexing)

Before a SQL string gets parsed, a **lexer/tokenizer** chops the raw text into **tokens** — the smallest meaningful units the parser recognises.

```
Source code → Tokens
```

### Example

```cpp
int x = 42;
```

turns into:

```
KEYWORD(int)
IDENTIFIER(x)
OPERATOR(=)
INTEGER_LITERAL(42)
SEMICOLON(;)
```

| Source | Token type |
|--------|------------|
| `int`  | keyword |
| `x`    | identifier |
| `42`   | integer literal |
| `"hello"` | string literal |
| `+`    | operator |

---

## 2. The compilation pipeline

```
Source Code
     ↓
Lexer / Tokenizer
     ↓
Tokens
     ↓
Parser
     ↓
AST
     ↓
Semantic Analysis
     ↓
Machine Code (or Bytecode)
```

Tokenization is the **first phase** inside compilation — it isn't a separate program, it's the opening stage of the compiler.

---

## 3. AST (Abstract Syntax Tree)

Once tokens exist, the **parser** builds an AST — a tree of the query's structure and meaning. Internal nodes are operators/keywords; leaves are values or column names.

### Example A — simple WHERE

```sql
WHERE id > 5
```

```
    [>]
   /   \
  id    5
```

### Example B — compound WHERE

```sql
WHERE (id > 2) AND ((id > 15) OR (id < 20))
```

```
          AND
         /   \
        >     OR
       / \   /  \
      id  2 >    <
           / \  / \
          id 15 id 20
```

### Example C — full query (`main.cpp`)

```sql
SELECT name FROM employees WHERE id >= 3
```

```
        SELECT
       /      \
   columns    FROM
     |          |
    name    employees
               |
             WHERE
               |
              [>=]
             /    \
            id     3
```

The engine **walks the tree** to decide which table to scan, which rows to keep, and which columns to return.

---

## Files

- `main.cpp` — tokenizer + recursive-descent parser + evaluator
