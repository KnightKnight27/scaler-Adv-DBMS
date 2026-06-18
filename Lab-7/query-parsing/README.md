# Query Parsing

Notes on the query-parsing implementation in `main.cpp`.

---

## 1. Tokenization (lexing)

Before source code or SQL gets parsed, a **lexer/tokenizer** chops the raw text into **tokens**.

```
Source code → Tokens
```

### Example

```sql
SELECT name FROM employees WHERE id >= 3
```

turns into:

```
KW_SELECT(SELECT)
NAME(name)
KW_FROM(FROM)
NAME(employees)
KW_WHERE(WHERE)
NAME(id)
GREATER_EQ(>=)
NUMERAL(3)
EOL
```

### So what's a token?

The **smallest meaningful unit** the parser recognises.

| Source | Token type |
|--------|------------|
| `SELECT`  | keyword |
| `name`    | identifier |
| `3`       | integer literal |
| `>=`      | operator |

---

## 2. The compilation pipeline (applied to SQL)

```
SQL Text
     ↓
Lexer / Tokenizer
     ↓
Tokens
     ↓
Parser (recursive-descent)
     ↓
AST (Abstract Syntax Tree)
     ↓
Evaluator / Executor
     ↓
Query Results
```

---

## 3. AST (Abstract Syntax Tree)

Once tokens exist, the **parser** consumes them and builds an AST — a tree of
the query's **structure and meaning**, not just its raw characters.

### Example — full query (from `main.cpp`)

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

- `SELECT` is the root of the whole query tree.
- Two branches: what to fetch (`columns → name`) and where from
  (`FROM → employees`).
- The `WHERE` clause hangs off `FROM` and carries a `>=` comparison over `id`
  and `3`.

**The point:** the AST captures the *logical shape* of the query. The engine
then **walks the tree** to decide what to do — which table to scan, which rows
to keep, which columns to return.

---
*Lab 7 — Piyush Pawan Kumar (24BCS10296)*
