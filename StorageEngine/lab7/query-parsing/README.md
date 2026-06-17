# Query Parsing

Scratch notes I kept while building `main.cpp`.

---

## 1. Tokenization (lexing)

Before C++/Java source gets parsed, a **lexer/tokenizer** chops the raw text
into **tokens**.

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

### So what's a token?

The **smallest meaningful unit** the compiler recognises.

| Source | Token type |
|--------|------------|
| `int`  | keyword |
| `x`    | identifier |
| `42`   | integer literal |
| `"hello"` | string literal |
| `+`    | operator |

### Worth remembering

The compiler doesn't turn every runtime string or number into tokens.
Tokenization runs over the **source text**, ahead of parsing.

```cpp
string s = "hello";
```

- The text `"hello"` becomes a **string literal token**.
- Once compiled, the real string object lives in **memory** — it's not a token
  any more.

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

---

## 3. Does tokenization run *before* compilation?

Not really — **it's part of the compiler.**

When you run:

```bash
g++ main.cpp
```

the compiler kicks off and one of its first jobs is:

1. Read the source
2. Tokenize (lexical analysis)
3. Parse
4. Build the AST
5. Emit machine code

So tokenization is the **first phase** *inside* compilation.

### Who runs it?

The **lexer/tokenizer** stage of the compiler.

- C++ → lexer inside GCC / Clang
- Java → lexer inside `javac`

### Where the lexer lives

```
Compiler
 ├─ Lexer (Tokenizer)
 ├─ Parser
 ├─ Semantic Analyzer
 └─ Code Generator
```

It isn't a separate program running first — it's usually the compiler's
**opening stage**.

---

## 4. AST (Abstract Syntax Tree)

Once tokens exist, the **parser** consumes them and builds an AST — a tree of
the query's **structure and meaning**, not just its raw characters.

Internal nodes are **operators / keywords**; leaves are **values or column
names**.

---

### Example A — simple WHERE

```sql
WHERE id > 5
```

```
    [>]
   /   \
  id    5
```

`>` is the root of the sub-tree; its children are the operands — column `id`
and literal `5`.

---

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

- `AND` sits on top because it joins the two big conditions.
- Left subtree: `id > 2`
- Right subtree: `id > 15 OR id < 20`, splitting into two comparisons under `OR`.

---

### Example C — full query (from `main.cpp`)

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
