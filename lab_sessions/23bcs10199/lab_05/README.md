# Lab 5 — Shunting-Yard Algorithm + Minimal SQL SELECT Parser

**Student:** Indrajeet Yadav | **Roll No:** 23BCS10199

---

## Objective

1. Implement Dijkstra's **Shunting-Yard algorithm** to evaluate infix arithmetic and boolean expressions — the mechanism SQL engines use for WHERE clause evaluation.
2. Build a **minimal SQL SELECT parser and executor** that handles `SELECT ... FROM ... WHERE ... ORDER BY ... LIMIT ...` against an in-memory `vector<Row>`.

---

## Build & Run

```bash
g++ -std=c++17 -Wall -Wextra -O2 sql_parser.cpp -o sql_parser
./sql_parser
```

---

## Part 1: Shunting-Yard Algorithm

### Why it Matters for Databases

SQL WHERE clauses contain infix expressions like:

```sql
WHERE age * 2 > 50 AND gpa >= 3.5
WHERE (salary / 1000 + bonus * 0.1) > 100
WHERE NOT (status = 'cancelled' OR amount < 0)
```

These expressions must be evaluated for every row the database scans. The challenge: infix notation requires knowing operator precedence and parenthesization. **Shunting-Yard converts infix to Reverse Polish Notation (RPN/postfix) in O(n) time**, after which evaluation is a simple stack pass with no precedence rules.

### The Algorithm (Dijkstra, 1961)

Maintain two data structures:
- **Output queue** — accumulates the RPN output
- **Operator stack** — temporarily holds operators

For each token:

```
Token type     Action
──────────────────────────────────────────────────────────────
Number/var  → push directly to OUTPUT
(           → push to OPERATOR STACK
)           → pop OPERATOR STACK to OUTPUT until '(' found; discard '('
Operator o1 → while STACK top is operator o2 AND
                (o2 has higher precedence OR
                 same precedence AND o1 is left-associative):
                  pop o2 to OUTPUT
              push o1 to OPERATOR STACK

After all tokens: pop remaining OPERATOR STACK to OUTPUT
```

### Worked Example

```
Expression:  3 + 4 * 2 - 1

Tokens:      3   +   4   *   2   -   1

Step 1: '3' → OUTPUT: [3]
Step 2: '+' → STACK: [+]
Step 3: '4' → OUTPUT: [3, 4]
Step 4: '*' → precedence('*')=7 > precedence('+')=6, push
              STACK: [+, *]
Step 5: '2' → OUTPUT: [3, 4, 2]
Step 6: '-' → precedence('-')=6 ≤ precedence('*')=7 → pop '*'
              OUTPUT: [3, 4, 2, *]
              precedence('-')=6 = precedence('+')=6, '-' left-assoc → pop '+'
              OUTPUT: [3, 4, 2, *, +]
              push '-': STACK: [-]
Step 7: '1' → OUTPUT: [3, 4, 2, *, +, 1]
Done:         pop '-': OUTPUT: [3, 4, 2, *, +, 1, -]

RPN: 3 4 2 * + 1 -

Evaluate:
  Stack: [3]
  Stack: [3, 4]
  Stack: [3, 4, 2]
  '*': pop 4,2 → push 8: Stack: [3, 8]
  '+': pop 3,8 → push 11: Stack: [11]
  Stack: [11, 1]
  '-': pop 11,1 → push 10: Stack: [10]
  Result: 10  ✓
```

### Right Associativity — Exponentiation

`^` (exponentiation) is right-associative: `2 ^ 3 ^ 2` = `2 ^ (3^2)` = `2^9` = 512, not `(2^3)^2` = 64.

When the current operator `o1` equals the stack-top operator `o2` in precedence AND `o1` is right-associative, we do NOT pop `o2`. This is the only difference between left and right associative handling.

```
2 ^ 3 ^ 2  (right-associative)
RPN: 2 3 2 ^ ^

Evaluate:
  [2, 3, 2]
  '^': pop 3,2 → 3^2 = 9: [2, 9]
  '^': pop 2,9 → 2^9 = 512: [512]  ✓
```

### Operator Precedence Table

| Level | Operators | Associativity |
|-------|-----------|---------------|
| 1 (lowest) | `OR`, `\|\|` | Left |
| 2 | `AND`, `&&` | Left |
| 3 | `NOT` | Right (unary) |
| 4 | `=`, `!=`, `<>` | Left |
| 5 | `<`, `>`, `<=`, `>=` | Left |
| 6 | `+`, `-` | Left |
| 7 | `*`, `/`, `%` | Left |
| 8 (highest) | `^` | Right |

This exactly mirrors SQL operator precedence: arithmetic before comparison, comparison before boolean.

---

## Part 2: SQL SELECT Parser + Executor

### Pipeline

```
SQL string  →  tokenize()  →  parse_select()  →  execute()  →  vector<Row>
               (lexer)        (parser)           (executor)
```

### Parser: `parse_select()`

The parser is a simple recursive-descent word-by-word reader. It handles:

```
SELECT col1, col2, ...   FROM table_name
  [WHERE expression]
  [ORDER BY col [ASC|DESC]]
  [LIMIT n]
```

Each clause is terminated by the next keyword. The `WHERE` clause raw string is stored unparsed — it is compiled into RPN only once before executing the scan, avoiding repeated parsing overhead.

### Executor: `execute()`

The executor follows the classical relational algebra pipeline:

```
Input: vector<Row> (full table data)
  │
  ├─ WHERE  filter  → eval_rpn() per row with row values as variables
  │                    Rows where result == 0.0 are rejected.
  │
  ├─ SELECT project  → if select_cols is non-empty, keep only named columns
  │                    if empty (SELECT *), keep all columns
  │
  ├─ ORDER BY sort   → std::stable_sort on the result set
  │                    numeric sort when possible; string sort fallback
  │
  └─ LIMIT truncate  → result.resize(limit_n)
```

### Row Representation

```cpp
using Value = std::variant<double, std::string>;

struct Row {
    std::unordered_map<std::string, Value> cols;
};
```

- `std::variant<double, std::string>` cleanly models SQL's typed columns without casting.
- `std::unordered_map` gives O(1) column lookup by name.
- String columns are converted to their numeric length for arithmetic expressions; a real database would use a type system to reject malformed expressions.

### WHERE Clause Evaluation per Row

For each row that passes the scan:

```cpp
VarMap vars;
for (auto& [col, val] : row.cols)
    vars[col] = col_as_double(row, col);  // extract numeric value

double passed = eval_rpn(where_rpn, vars);
if (passed == 0.0) continue;  // reject
```

The RPN tokens were compiled **once** before the loop, not per row. This is how real database engines work: parse the WHERE expression into an expression tree at plan time, then evaluate the tree per row.

---

## How This Maps to PostgreSQL

### Tokenizer

```
PostgreSQL: src/backend/parser/scan.l (flex lexer)
This lab:   tokenize() — hand-written character-by-character scanner
```

### Parser

```
PostgreSQL: src/backend/parser/gram.y (bison grammar, ~15,000 lines)
This lab:   parse_select() — word-by-word for SELECT/FROM/WHERE/ORDER BY/LIMIT
```

### Expression Evaluation

```
PostgreSQL:
  parse → RawExpr → transformExpr() → Expr (typed AST)
  plan  → ExprState (compiled expression)
  exec  → ExecEvalExpr() per tuple
    Uses a tree of eval function pointers, not RPN.
    But the precedence rules that produce the AST came from the grammar,
    which encodes the same precedence as the Shunting-Yard table.

This lab:
  tokenize() → shunting_yard() → eval_rpn() per row
  Functionally equivalent; different representation (RPN vs AST).
```

### ORDER BY

```
PostgreSQL: ExecSort node → tuplesort_performsort() → external merge sort
            Spills to disk if sort set > work_mem (typically 4 MB)
This lab:   std::stable_sort on vector<Row> — purely in-memory
```

### LIMIT

```
PostgreSQL: Limit node wraps the Sort node; stops fetching tuples after N rows.
            Enables early termination before the full sort in many cases.
This lab:   result.resize(limit_n) — truncate after sort.
```

---

## Shunting-Yard Step-by-Step Trace

### Expression: `age * 2 > 50 AND gpa >= 3.5`

```
Tokens: age * 2 > 50 AND gpa >= 3.5

Step   Token    Stack              Output
──────────────────────────────────────────────────
  1    age      []                 [age]
  2    *        [*]                [age]
  3    2        [*]                [age, 2]
  4    >        [>]                [age, 2, *]   pop * (prec 7 > prec 5)
  5    50       [>]                [age, 2, *, 50]
  6    AND      [AND]              [age, 2, *, 50, >]  pop > (prec 5 > prec 2)
  7    gpa      [AND]              [age, 2, *, 50, >, gpa]
  8    >=       [AND, >=]          [age, 2, *, 50, >, gpa]  >= prec 5 > AND prec 2
  9    3.5      [AND, >=]          [age, 2, *, 50, >, gpa, 3.5]
 end   (done)   []                 [age, 2, *, 50, >, gpa, 3.5, >=, AND]

RPN: age 2 * 50 > gpa 3.5 >= AND

With age=30, gpa=3.8:
  [30]
  [30, 2]
  * → [60]
  [60, 50]
  > → 60>50=1 → [1]
  [1, 3.8]
  [1, 3.8, 3.5]
  >= → 3.8>=3.5=1 → [1, 1]
  AND → 1&&1=1 → [1]
  Result: 1 (true) → row passes WHERE filter
```

---

## Key Takeaways

1. **Shunting-Yard converts infix to RPN in O(n) time with O(n) space** — one pass through tokens with a stack. No recursion needed.

2. **Operator precedence and right-associativity are the two input parameters** that fully determine the RPN output. The algorithm itself has no knowledge of what `*` means.

3. **Compile the WHERE expression once; evaluate per row.** Tokenize and convert to RPN once before the scan loop, then call `eval_rpn()` per row with the row's column values as variables.

4. **A minimal SQL executor is a four-stage pipeline:** filter (WHERE) → project (SELECT columns) → sort (ORDER BY) → truncate (LIMIT). Each stage is an independent operation on a `vector<Row>`.

5. **`std::variant` models SQL's typed columns cleanly** without a class hierarchy or casting overhead — `std::get_if` dispatches at zero cost.

6. **Real databases add a planner between parse and execute** — it decides whether to use an index, which join algorithm to use, whether to parallelize, etc. The executor itself looks very similar to what is implemented here.
