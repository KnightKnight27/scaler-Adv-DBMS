# Lab 7 — SQL WHERE Clause Parsing: Two Approaches

## Concept

A SQL WHERE clause is an infix boolean expression that a database must parse and evaluate against rows. There are two standard ways to do this:

1. **Shunting-Yard (postfix evaluation)** — convert the infix expression to RPN once, then evaluate using a stack. Simple and fast, but the "AST" is implicit in the RPN sequence.
2. **Recursive Descent Parser** — build an explicit AST (Abstract Syntax Tree) by parsing grammar rules as recursive function calls. More structured, easier to extend with new clauses (BETWEEN, IS NULL, etc.).

Both are implemented here against an in-memory `Employee` table to show the same query producing the same results via two different mechanisms.

## Part 1: dijkstraShunting.cpp — Postfix Evaluator

### Approach
- **Tokenize** the WHERE string into identifiers, integer literals, comparison operators (`>`, `<`, `>=`, `<=`, `=`, `!=`), logical operators (`AND`, `OR`), and parentheses.
- **Shunting-Yard** converts the token stream to postfix using an operator stack and precedence table: `OR(1) < AND(2) < comparisons(3-4)`.
- **Evaluate** the postfix sequence with an integer stack: push literals/column values, on operator pop two values and push the boolean result (0 or 1).

### Result for `id > 3 AND (age < 25 OR age >= 30)`:
```
Postfix: id 3 > age 25 < age 30 >= OR AND
Matches: Sneha(4,21), Vivaan(5,20), Ishaan(6,31), Meera(7,22), Devansh(8,33)
```

## Part 2: queryParsing.cpp — Recursive Descent Parser

### Approach
Parse the WHERE clause into an explicit AST using grammar rules:

```
query      → SELECT col FROM table WHERE expr
expr       → or_expr
or_expr    → and_expr ( OR and_expr )*
and_expr   → factor ( AND factor )*
factor     → '(' or_expr ')' | comparison
comparison → col op integer
```

Each grammar rule becomes a function (`parseOr`, `parseAnd`, `parseFactor`, `parseComparison`). The result is a tree of `Node` structs where internal nodes are AND/OR and leaves are comparisons like `age < 25`.

Evaluation walks the AST recursively: AND → both children must be true, OR → either child must be true, leaf → evaluate the comparison against the row.

### Results:
```
SELECT name FROM employees WHERE id >= 3 OR age < 20
→ Rama Krishnan, Karan, Sneha, Vivaan, Ishaan, Meera, Devansh

SELECT name FROM employees WHERE id > 3 AND age >= 30
→ Ishaan, Devansh

SELECT id FROM employees WHERE (age < 25 AND id != 2) OR age >= 30
→ 1, 3, 4, 5, 6, 7, 8
```

## Shunting-Yard vs Recursive Descent

| | Shunting-Yard | Recursive Descent |
|---|---|---|
| Output | Implicit postfix sequence | Explicit AST nodes |
| Complexity | Simple stack loop | One function per grammar rule |
| Extensibility | Add operators to table | Add grammar rule + function |
| Real-world use | Expression calculators | SQL parsers, compilers |

A real database uses recursive descent (or a parser generator) to produce an AST, then passes it to the planner. Shunting-Yard is the underlying mechanism that handles operator precedence in both cases.
