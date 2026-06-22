# Lab 5 — Shunting-Yard Expression Evaluator & a Minimal SQL `SELECT` Engine

## Aim

Build, in a single C++17 program, two small but complete language processors:

1. **Part A** — Dijkstra's *Shunting-Yard* algorithm, which converts an infix
   arithmetic expression into Reverse Polish Notation (RPN / postfix) and then
   evaluates it.
2. **Part B** — a tiny SQL engine that parses
   `SELECT <cols|*> FROM <table> WHERE <condition>` and runs the query against
   an in-memory `std::vector<Row>`.

Both parts share the same skeleton every real language tool uses: **lex →
parse → evaluate**. The point of the lab is to see *why* that pipeline is
structured the way it is.

## Build & Run

```
g++ -std=c++17 *.cpp -o lab5
./lab5
```

Everything lives in `expr_eval.cpp`.

---

## Part A — How Shunting-Yard Works

### The problem it solves

Infix notation (`3 + 4 * 2`) is convenient for humans but awkward for a
machine, because the *meaning* of a symbol depends on what surrounds it: the
`+` must wait for `4 * 2` to finish. Postfix notation (`3 4 2 * +`) removes
that ambiguity entirely — there are no parentheses and no precedence rules at
evaluation time, because operand order already encodes everything. So the trick
is to do the hard reasoning **once**, during conversion, and leave evaluation
trivial.

### The two-storage-area idea

Shunting-Yard streams the tokens left to right and maintains exactly two
storage areas:

* an **output queue** — the postfix result being built, and
* an **operator stack** — operators that are *waiting* to find out whether
  something to their right binds tighter.

The rules per token are short:

* **Number** → push straight to the output queue. Operands never wait.
* **Operator `o1`** → while the operator on top of the stack should be applied
  *before* `o1`, pop it into the output. Then push `o1`.
* **`(`** → push onto the stack; it is a fence.
* **`)`** → pop operators into the output until the matching `(` is found, then
  discard the `(`.
* **End of input** → flush every remaining operator to the output.

### Precedence and associativity — the heart of it

"Should the stacked operator be applied first?" is decided by a small
precedence/associativity table. In the code:

```
^  precedence 4, right-associative
* / %  precedence 3, left-associative
+ -    precedence 2, left-associative
u (unary minus)  precedence 5, right-associative
```

The single decisive line is:

```cpp
bool pop = (top.precedence > cur.precedence) ||
           (top.precedence == cur.precedence && !cur.rightAssoc);
```

We pop the stacked operator when it binds *strictly tighter*, **or** when it
binds *equally* and the incoming operator is **left-associative**. That one
condition produces both behaviours we want:

* `2 ^ 3 ^ 2` → because `^` is right-associative, an incoming `^` does **not**
  pop an equal-precedence `^`, so the second `^` is evaluated first, giving
  `2^(3^2) = 512` (verified in the sample output).
* `8 - 3 - 2` would, with left-associativity, pop the first `-` when the second
  arrives, yielding `(8-3)-2` as expected.

### Unary minus

A `-` is unary when it appears at the start of the input, right after another
operator, or right after `(`. The lexer detects this from context and emits a
synthetic operator `u` with very high precedence and right-associativity, so
`-2 ^ 2` and `3 * -2` behave correctly. During evaluation `u` pops a single
operand instead of two.

### Why it avoids recursion

A naïve evaluator would use a recursive-descent grammar with one function per
precedence level. Shunting-Yard instead makes the **operator stack** do the job
the call stack would otherwise do — every "deferred" operator sits on the
explicit stack rather than in a suspended function frame. The result is a
single linear pass (`O(n)` tokens, each pushed and popped at most once) with no
recursion at all, which is why production calculators and many bytecode
compilers favour it.

### Evaluating the RPN

The second pass is almost embarrassingly simple. Walk the postfix list: push
numbers; on an operator pop its operands, apply, push the result. When the list
is exhausted, the single remaining stack value is the answer. No precedence
logic appears here because the conversion already baked it in.

---

## Part B — How the SQL Engine Works

The SQL side follows the canonical compiler front-end:
**lexer → tokens → recursive-descent parser → AST → evaluation over rows.**

### Lexer (tokeniser)

`lex()` scans the query string and produces a flat `vector<SqlToken>`. It
recognises:

* keywords `SELECT FROM WHERE AND OR` (case-insensitive — they are matched
  *after* an identifier is scanned),
* punctuation `* , ( )`,
* comparison operators `= != < <= > >=` (two-character operators are checked
  before single-character ones so `<=` isn't read as `<` then `=`),
* numeric literals, single-quoted string literals, and identifiers.

Keeping the lexer dumb (it knows nothing about grammar) keeps the parser clean.

### Grammar (BNF-ish)

```
query       ::= "SELECT" select_list "FROM" identifier [ "WHERE" or_expr ]

select_list ::= "*"
              | identifier { "," identifier }

or_expr     ::= and_expr { "OR" and_expr }
and_expr    ::= comparison { "AND" comparison }
comparison  ::= primary [ cmp_op primary ]
primary     ::= "(" or_expr ")"
              | identifier            (* a column reference *)
              | number
              | string

cmp_op      ::= "=" | "!=" | "<" | "<=" | ">" | ">="
```

### Recursive-descent parser

There is one function per grammar rule, and the *call chain encodes operator
precedence*. `parseOr` calls `parseAnd`, which calls `parseComparison`, which
calls `parsePrimary`. Because OR sits at the top of the chain and AND below it,
**AND binds tighter than OR automatically** — no precedence table is needed
here, the recursion structure *is* the table. Parentheses are handled in
`parsePrimary`, which loops back to `parseOr`, giving full nesting.

The parser builds an **AST** of `Expr` nodes with three shapes:

* `Column` — a reference like `gpa`,
* `Literal` — a number or string constant,
* `Binary` — either a comparison (`gpa >= 3.5`) or a logical connective
  (`AND` / `OR`) with two children.

This mirrors how real databases work: parsing produces a tree, and the tree —
not the original text — is what gets executed.

### Evaluation over rows

A `Row` is a `std::map<std::string, Value>` where a `Value` is tagged as either
a number or a string. `runQuery` iterates the table and, for each row, calls
`evalPredicate` on the WHERE AST:

* a `Binary` AND/OR node recurses into both children and combines them with C++
  `&&` / `||` (which gives short-circuit evaluation for free),
* a comparison node resolves each side to a concrete `Value` (a column lookup or
  a literal) and compares them — numerically when both sides are numbers,
  lexically otherwise.

Rows whose predicate is true are projected: `SELECT *` prints every column,
otherwise only the named columns are shown. A missing `WHERE` clause is treated
as "always true", so `SELECT * FROM students` returns everything.

### Connection to real query engines

This is a faithful miniature of what a production engine such as PostgreSQL or
SQLite does to a `WHERE` clause. The query text is lexed and parsed into an
**AST / parse tree**; the predicate becomes an *expression tree* of comparisons
joined by boolean operators; and execution walks that tree once per candidate
row (here a full table scan). Real systems then layer optimisations on top —
index lookups to avoid scanning every row, predicate push-down, constant
folding, and converting the tree into a physical plan — but the semantic core,
"evaluate a boolean expression tree against each row", is exactly what this lab
implements. Understanding this AST-and-evaluate model is the bridge between the
SQL a user types and the rows a database returns.

---

## Sample Output

```
=== PART A : Shunting-Yard expression evaluator ===

  infix : 3 + 4 * 2
  RPN   : 3 4 2 * +
  result: 11.0000

  infix : (3 + 4) * 2
  RPN   : 3 4 + 2 *
  result: 14.0000

  infix : 2 ^ 3 ^ 2
  RPN   : 2 3 2 ^ ^
  result: 512.0000          (right-associative: 2^(3^2))

  infix : -5 + 3 * -2
  RPN   : 5 (-) 3 2 (-) * +
  result: -11.0000          (unary minus handled)

=== PART B : minimal SQL SELECT engine ===

  query : SELECT name, gpa FROM students WHERE gpa >= 3.5
    name=Alice | gpa=3.9
    name=Charlie | gpa=3.5
  (2 row(s) matched)

  query : SELECT id, name FROM students WHERE age > 20 AND gpa < 3.0
    id=2 | name=Bob
    id=5 | name=Eve
  (2 row(s) matched)

  query : SELECT * FROM students WHERE (gpa > 3.0 AND age < 21) OR id = 5
    age=20 | gpa=3.9 | id=1 | name=Alice
    age=19 | gpa=3.5 | id=3 | name=Charlie
    age=21 | gpa=2.4 | id=5 | name=Eve
  (3 row(s) matched)
```

## Conclusion

Both halves of the lab show the same lesson from two angles. Shunting-Yard
turns an ambiguous infix string into an unambiguous postfix form by resolving
precedence and associativity *once* using an explicit operator stack instead of
recursion. The SQL engine turns a query string into an AST and evaluates the
`WHERE` predicate as a boolean expression tree, exactly mirroring how real
query engines decide which rows satisfy a filter. The recurring theme — lex,
parse into a structure, then evaluate that structure — is the foundation of
every interpreter, compiler, and database query processor.
```
