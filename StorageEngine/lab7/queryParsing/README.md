# Query Parsing — Learning Notes

This is where I jot down what I learn while writing the code in `main.cpp`.

---

## 1. Tokenization (Lexing)

Before Java/C++ code is parsed, a **lexer/tokenizer** breaks the source code into **tokens**.

```
Source code → Tokens
```

### Example

```cpp
int x = 42;
```

becomes:

```
KEYWORD(int)
IDENTIFIER(x)
OPERATOR(=)
INTEGER_LITERAL(42)
SEMICOLON(;)
```

### What is a token?

A token is the **smallest meaningful unit** recognized by the compiler.

| Source | Token type |
|--------|------------|
| `int`  | keyword token |
| `x`    | identifier token |
| `42`   | integer literal token |
| `"hello"` | string literal token |
| `+`    | operator token |

### Important

The compiler does **not** convert every string or number into tokens at runtime.
Tokenization happens on the **source code text**, before parsing.

```cpp
string s = "hello";
```

- The source text `"hello"` is tokenized as a **string literal token**.
- After compilation, the actual string object exists in **memory** — it is no longer a token.

---

## 2. The Compilation Pipeline

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

---

## 3. Does tokenization happen *before* compilation?

Not really. **Tokenization is part of the compiler itself.**

When you run:

```bash
g++ main.cpp
```

the compiler starts, and one of the first things it does is:

1. Read source code
2. Tokenize (lexical analysis)
3. Parse
4. Build AST
5. Generate machine code

So tokenization happens **during** compilation, as the **first phase**.

### Who does the tokenization?

The **lexer/tokenizer** component of the compiler.

- C++ → lexer inside GCC / Clang
- Java → lexer inside `javac`

### Where the lexer sits

```
Compiler
 ├─ Lexer (Tokenizer)
 ├─ Parser
 ├─ Semantic Analyzer
 └─ Code Generator
```

The tokenizer is **not** a separate thing running before the compiler — it is usually the compiler's **first stage**.

---

## 4. AST (Abstract Syntax Tree)

After tokens are produced, the **parser** reads them and builds an AST — a tree that represents the **structure and meaning** of the query, not just the raw text.

Each internal node is an **operator or keyword**, and each leaf is a **value or column name**.

---

### Example A — Simple WHERE clause

```sql
WHERE id > 5
```

```
    [>]
   /   \
  id    5
```

The `>` operator is the root of this sub-tree. Its two children are the operands: column `id` and literal `5`.

---

### Example B — Compound WHERE clause

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

- `AND` sits at the top because it combines the two big conditions.
- Left subtree: `id > 2`
- Right subtree: `id > 15 OR id < 20`, which itself splits into two comparison nodes under `OR`.

---

### Example C — Full SQL query (from `main.cpp`)

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

- The `SELECT` node is the root of the whole query tree.
- It has two main branches: what to fetch (`columns → name`) and where to fetch from (`FROM → employees`).
- The `WHERE` clause hangs off the `FROM` node and holds a `>=` comparison with `id` and `3` as its children.

**Key idea:** The AST captures the *logical structure* of the query. The database engine then **walks this tree** to figure out what to do — which table to scan, which rows to filter, which columns to return.
