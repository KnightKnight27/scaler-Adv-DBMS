# Lab 5 — Shunting-Yard Algorithm + Minimal SQL SELECT Parser

## Concept

A SQL WHERE clause is an infix boolean expression: `age > 25 AND salary * 1.1 < 90000`. Before a database can evaluate it, it needs to convert the infix form into something a stack machine can execute — that's **Reverse Polish Notation (RPN / postfix)**. Dijkstra's **Shunting-Yard** algorithm does this conversion in O(n) using two stacks.

Once we can evaluate expressions, we can build a minimal SQL executor: parse SELECT → filter rows with WHERE (via Shunting-Yard) → project columns → sort → limit.

## Part 1: Shunting-Yard

### Approach
Operators have precedence and associativity (e.g. `*` binds tighter than `+`, `^` is right-associative). The algorithm processes tokens left-to-right:
- **Number/identifier** → push directly to output queue
- **Operator** → pop operators from stack to output while they have higher (or equal left-associative) precedence, then push current operator
- **`(`** → push to stack
- **`)`** → pop to output until matching `(` is found

The result is a postfix sequence that a simple stack machine can evaluate left-to-right: push operands, on operator pop two values, apply, push result.

### SQL operator table (precedence low → high):
```
OR(1) < AND(2) < = != (3) < < > <= >= (4) < + - (5) < * / (6) < ^ (7)
```

### Result:
```
Expression: age * 2 + gpa / 1.0 > 45
RPN:        age 2 * gpa 1.0 / + 45 >
Eval (age=22, gpa=3.8): true   (22*2 + 3.8 = 47.8 > 45)
```

## Part 2: SQL Parser over vector\<Row\>

### Approach
Rather than a full parser, we split the problem:
1. **Tokenize** the SQL string into keywords, identifiers, operators, literals.
2. **Parse** using a simple word-by-word scan: consume SELECT → column list → FROM → table name → optional WHERE/ORDER BY/LIMIT clauses.
3. **Execute** by iterating over the pre-fetched `vector<Row>`:
   - Compile the WHERE clause to RPN once
   - For each row, build a variable map (column → value) and call `eval_rpn()`
   - Project only the requested columns
   - Sort by ORDER BY column
   - Truncate to LIMIT

Using `std::variant<double, string>` as the value type lets us handle both numeric and string columns cleanly.

### Result:
```sql
SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3
→ Alice(3.8), Carol(3.5), Dave(3.1)

SELECT * FROM students WHERE age >= 22 && age <= 26
→ Alice(22), Bob(25)
```

## Key Takeaway

Shunting-Yard is the engine behind SQL expression evaluation. A real database compiles the WHERE clause once into an expression tree during planning — it doesn't re-parse per row. The `vector<Row>` here simulates what the storage layer returns after fetching pages from disk (as in Lab 3).
