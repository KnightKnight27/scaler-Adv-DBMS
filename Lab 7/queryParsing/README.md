# Query Parsing — Recursive Descent Parser

Parses a SQL `SELECT ... FROM ... WHERE ...` query string into an AST and evaluates it against a set of rows.

---

## How it works

### 1. Tokenization

The raw query string is broken into tokens — the smallest meaningful units.

```
SELECT name FROM employees WHERE id >= 3
```

becomes:

```
SELECT  IDENTIFIER(name)  FROM  IDENTIFIER(employees)  WHERE  IDENTIFIER(id)  GTE  NUMBER(3)  END
```

### 2. Parsing → AST

The token stream is fed into a recursive-descent parser that builds an **Abstract Syntax Tree (AST)**. Each node is either an operator or a leaf value.

```sql
WHERE id >= 3
```

```
   [>=]
   /   \
  id    3
```

For compound conditions with `OR`:

```sql
WHERE id > 2 OR age < 25
```

```
      OR
     /  \
   [>]  [<]
   / \  / \
  id  2 age 25
```

### 3. Evaluation

`applyFilter` walks the AST recursively. For each `BinaryExpression` node, it resolves both sides to integers and applies the operator. `OR` nodes recurse down both branches.

---

## Grammar (what the parser handles)

```
expression  → primary ( "OR" primary )*
primary     → "(" expression ")" | condition
condition   → IDENTIFIER operator NUMBER
operator    → ">" | "<" | ">=" | "<="
```

---

## Build & Run

```bash
g++ -std=c++17 main.cpp -o main && ./main
```
