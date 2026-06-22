# Lab 7: SQL Query Parsing & Expression Evaluation

**Course:** Advanced DBMS (Scaler)
**Author:** Praveen Kumar 24bcs10048
**Date:** 2026-06-22

---

## 1. Objective

Implement two components of a database query front-end in C++:
- **Part A**: A lexer and recursive-descent parser that tokenizes and evaluates SQL SELECT...WHERE queries against an in-memory table.
- **Part B**: Dijkstra's shunting-yard algorithm to convert infix expressions (including SQL WHERE clauses) to postfix and evaluate them with a stack.

---

## 2. Build and Run

```bash
# Part A
g++ -std=c++17 -O2 -o query_parser query_parser.cpp
./query_parser

# Part B
g++ -std=c++17 -O2 -o shunting_yard shunting_yard.cpp
./shunting_yard

# Both at once
make run
```

---

## 3. Background: The Query Processing Pipeline

When you run `SELECT * FROM employees WHERE age > 25 AND dept = Engineering`, a database does not interpret that string character by character at query time. It runs it through a structured pipeline:

```
SQL string
    |
    v
Lexer (Tokenizer)
    |  breaks the string into smallest meaningful units (tokens)
    v
Parser
    |  validates grammar, builds an Abstract Syntax Tree (AST)
    v
Semantic Analysis
    |  resolves column names, checks types
    v
Query Optimizer
    |  chooses the cheapest execution plan
    v
Executor
    |  runs the plan against actual data
    v
Result rows
```

This lab implements the first two stages (lexer and parser) and a simplified version of the executor (evaluating the AST against an in-memory table).

---

## 4. Part A: SQL Query Parser

### 4.1 Lexer (Tokenizer)

The lexer reads the raw SQL string character by character and groups characters into tokens. Each token has a type and a text value.

Token types used:

| Type | Example | Description |
|------|---------|-------------|
| KW_SELECT | SELECT | SQL keyword |
| KW_FROM | FROM | SQL keyword |
| KW_WHERE | WHERE | SQL keyword |
| KW_AND | AND | Logical operator |
| KW_OR | OR | Logical operator |
| KW_NOT | NOT | Logical negation |
| NAME | age, dept | Column name or unquoted value |
| INT_LIT | 25 | Integer literal |
| STR_LIT | 'hello' | Quoted string literal |
| EQ, LT, GT, LE, GE, NE | =, <, >, <=, >=, != | Comparison operators |
| LPAREN, RPAREN | ( ) | Grouping |
| END | -- | End of input |

Tokenization trace for `SELECT * FROM employees WHERE NOT dept = Marketing`:

```
KW_SELECT    "SELECT"
STAR         "*"
KW_FROM      "FROM"
NAME         "employees"
KW_WHERE     "WHERE"
KW_NOT       "NOT"
NAME         "dept"
EQ           "="
NAME         "Marketing"
END          ""
```

### 4.2 Grammar

The parser uses recursive descent with the grammar below. The depth of the call tree encodes operator precedence -- functions called later bind tighter.

```
query      -> SELECT * FROM NAME WHERE expr
expr       -> term (OR term)*         -- OR has lowest precedence
term       -> factor (AND factor)*    -- AND is tighter than OR
factor     -> NOT factor
            | LPAREN expr RPAREN
            | comparison
comparison -> NAME op (INT_LIT | STR_LIT | NAME)
op         -> = | < | > | <= | >= | !=
```

### 4.3 AST (Abstract Syntax Tree)

For `age > 25 AND dept = Engineering OR id = 1`:

```
          OR
         /  \
       AND    Cmp(id = 1)
      /   \
Cmp(age>25) Cmp(dept=Engineering)
```

The tree structure encodes precedence -- AND children are evaluated before OR parents. Evaluation is a simple in-order tree walk: at each node return `lhs.eval() && rhs.eval()` (for AND) or `||` (for OR).

### 4.4 Sample Queries and Results

```
SQL : SELECT * FROM employees WHERE age > 28
 id | name     | age | dept
  1 | Alice    |  28 -> NO (28 is not > 28)
  2 | Bob      |  35 -> YES
  5 | Eve      |  31 -> YES
  ...

SQL : SELECT * FROM employees WHERE age < 30 AND dept = Engineering
  1 | Alice    |  28 | Engineering  -> YES
  3 | Carol    |  22 | Engineering  -> YES
  8 | Heidi    |  29 | Engineering  -> YES
  3 row(s) matched

SQL : SELECT * FROM employees WHERE (age > 30 OR dept = HR) AND age <= 40
  2 | Bob      |  35 | Marketing    -> YES (age > 30)
  4 | Dave     |  40 | HR           -> YES (dept = HR and age <= 40)
  5 | Eve      |  31 | Engineering  -> YES (age > 30)
  9 | Ivan     |  33 | Marketing    -> YES
 10 | Judy     |  38 | HR           -> YES
  5 row(s) matched
```

---

## 5. Part B: Shunting-Yard Algorithm

### 5.1 The Problem: Infix vs. Postfix

Infix notation (`2 + 3 * 4`) is comfortable to write but requires precedence rules to parse correctly. A naive left-to-right evaluator would compute `(2+3)*4 = 20` instead of the correct `2+(3*4) = 14`.

Postfix (Reverse Polish Notation) has no parentheses and no precedence rules -- token order alone determines evaluation order. A single left-to-right stack-based pass evaluates it:

```
Postfix: 2 3 4 * +
Read 2  -> push          stack: [2]
Read 3  -> push          stack: [2, 3]
Read 4  -> push          stack: [2, 3, 4]
Read *  -> pop 4,3; push 12  stack: [2, 12]
Read +  -> pop 12,2; push 14 stack: [14]
Result: 14
```

### 5.2 The Algorithm (Dijkstra 1961)

```
output = []
op_stack = []

for each token t:
    if t is a NUMBER:
        output.append(t)

    elif t is an OPERATOR:
        while op_stack not empty
              AND op_stack.top is an operator
              AND (top.prec > t.prec
                   OR (top.prec == t.prec AND t is left-associative)):
            output.append(op_stack.pop())
        op_stack.push(t)

    elif t is LPAREN:
        op_stack.push(t)

    elif t is RPAREN:
        while op_stack.top != LPAREN:
            output.append(op_stack.pop())
        op_stack.pop()   # discard the LPAREN

drain op_stack to output
```

### 5.3 Operator Precedence Table

| Operator | Precedence | Associativity | Notes |
|----------|-----------|---------------|-------|
| OR | 1 | Left | Lowest |
| AND | 2 | Left | Tighter than OR |
| =, !=, <, >, <=, >= | 3 | Left | Comparisons |
| +, - | 4 | Left | Addition |
| *, / | 5 | Left | Multiplication |
| ^ | 6 | Right | Exponentiation |
| NOT | 7 | Right | Highest, unary |

### 5.4 Traced Examples

**`2 + 3 * 4`**

```
Token  | Output queue      | Op stack
-------+-------------------+---------
2      | 2                 |
+      | 2                 | +
3      | 2 3               | +
*      | 2 3               | + *       <- * has higher prec, push
4      | 2 3 4             | + *
[drain]| 2 3 4 *           | +
[drain]| 2 3 4 * +         |
Final postfix: 2 3 4 * +   -> 14
```

**`(2 + 3) * 4`**

```
Token  | Output queue      | Op stack
-------+-------------------+---------
(      |                   | (
2      | 2                 | (
+      | 2                 | ( +
3      | 2 3               | ( +
)      | 2 3 +             |           <- drain until LPAREN, discard it
*      | 2 3 +             | *
4      | 2 3 + 4           | *
[drain]| 2 3 + 4 *         |
Final postfix: 2 3 + 4 *   -> 20
```

### 5.5 SQL WHERE Clause Precedence

`AND` binds tighter than `OR`, just like `*` before `+`. This means:

```sql
WHERE age < 25 AND dept = HR OR id = 1
```

parses as:

```sql
WHERE (age < 25 AND dept = HR) OR id = 1
```

The shunting-yard algorithm handles this automatically with the precedence table.

---

## 6. Recursive Descent vs. Shunting-Yard

| Aspect | Recursive Descent (Part A) | Shunting-Yard (Part B) |
|--------|---------------------------|------------------------|
| Output | AST (tree in memory) | Flat postfix list |
| Precedence | Encoded in call depth | Encoded in operator table |
| Evaluation | Tree walk (recursive) | Stack pass (iterative) |
| Used by | PostgreSQL parser, MySQL | Calculator chips, some DBs |
| Extensibility | Easy to add new node types | Easy to add new operators |

Both solve the same problem. Recursive descent builds a tree that's easy to further analyze (optimize, transform). Shunting-yard outputs a flat list that's minimal and evaluates in a single linear pass.

---

## 7. How Real Databases Use This

**PostgreSQL**: Uses a hand-written lexer (`scan.l` via flex) and a yacc/bison grammar (`gram.y`, ~15000 lines). The WHERE clause becomes a tree of Node structs evaluated by `ExecQual()` in the executor. The expression evaluator uses a strategy similar to recursive descent.

**SQLite**: The tokenizer is in `tokenize.c`. The parser is `parse.y` (a Lemon-generated LALR parser). WHERE clause evaluation goes through the virtual machine (VDBE) which uses stack-based postfix evaluation -- close to shunting-yard.

**Query optimizers** often convert WHERE clauses into conjunctive normal form (CNF) -- a series of AND-joined conditions. This transformation uses the same AST structure we built in Part A.

---

## 8. Sample Output

```
============================================================
  Lab 7 -- Part A: SQL Query Parser
============================================================

  Table: employees (10 rows)
  ----+----------+-----+-------------
   id | name     | age | dept
  ----+----------+-----+-------------
    1 | Alice    |  28 | Engineering
    2 | Bob      |  35 | Marketing
  ...

  SQL : SELECT * FROM employees WHERE age > 28
  AST : (age > 28)
   id | name     | age | dept
    2 | Bob      |  35 | Marketing
    4 | Dave     |  40 | HR
    5 | Eve      |  31 | Engineering
    7 | Grace    |  45 | HR
    9 | Ivan     |  33 | Marketing
   10 | Judy     |  38 | HR
  6 row(s) matched

============================================================
  Lab 7 -- Part B: Shunting-Yard Expression Evaluator
============================================================

  Infix   : 2 + 3 * 4
  Postfix : 2 3 4 * +
  Result  : 14

  Infix   : (2 + 3) * 4
  Postfix : 2 3 + 4 *
  Result  : 20

  Infix   : 2 ^ 3 ^ 2
  Postfix : 2 3 2 ^ ^
  Result  : 512
```

---

## 9. Complexity

| Operation | Time | Space |
|-----------|------|-------|
| Lexing | O(n) characters | O(t) tokens |
| Parsing | O(t) tokens | O(t) AST nodes |
| Evaluation against m rows | O(m * t) | O(1) extra |
| Shunting-yard conversion | O(t) | O(t) stack |
| RPN evaluation | O(t) | O(t) stack |

---

## 10. Files in This Submission

| File | Description |
|------|-------------|
| `query_parser.cpp` | Part A: lexer, recursive-descent parser, AST, evaluator |
| `shunting_yard.cpp` | Part B: shunting-yard algorithm and RPN evaluator |
| `Makefile` | Build instructions for both programs |
| `README.md` | Quick-start guide |
| `Assignment.md` | This document |

---

## 11. References

- Aho, A.V. et al. *Compilers: Principles, Techniques, and Tools* (Dragon Book), Ch. 2-4
- Dijkstra, E.W. "Algol 60 translation" (1961) -- original shunting-yard paper
- PostgreSQL source: `src/backend/parser/gram.y`, `src/backend/executor/execQual.c`
- SQLite source: `src/tokenize.c`, `src/parse.y`
