# Recursive-Descent Query Parser

`main.cpp` takes a small `SELECT ... FROM ... WHERE ...` statement, lexes it,
parses the `WHERE` clause into an **AST**, and walks that tree once per row to
filter. Build & run:

```
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

## The idea

A `WHERE` clause like `marks >= 80 AND (age < 20 OR id = 5)` has precedence
rules: comparisons bind tightest, then `AND`, then `OR`. Instead of carrying a
precedence table around, this approach **bakes precedence into the grammar** —
each rule calls the next-tighter rule, so the tree comes out shaped correctly.

```
query  := SELECT Name FROM Name WHERE expr
expr   := term  (OR  term)*      # OR loosest  -> sits highest in the tree
term   := factor (AND factor)*   # AND tighter
factor := '(' expr ')' | Name Cmp Int   # comparisons tightest -> leaves
```

## Pieces

1. **Lexer** (`lex`) — one pass over the string producing a flat
   `vector<Token>`: keywords (`SELECT`/`FROM`/`WHERE`/`AND`/`OR`), identifiers,
   numbers, comparison ops (`> < >= <= =`), and parens.
2. **Parser** (`Parser`) — recursive descent following the grammar above. Each
   `OR` / `AND` becomes a `BinOp` node; each comparison becomes a `BinOp` over a
   `Column` and a `Number`.
3. **AST** — a single tagged `Expr` struct (`Column` / `Number` / `BinOp`)
   rather than a virtual class hierarchy, so the evaluator is short and needs no
   `dynamic_cast`.
4. **Evaluator** (`eval`) — a recursive tree walk: `AND`/`OR` recurse on both
   sides; a comparison reads the column from the row and compares to the number.

## Output

```
Query: SELECT name FROM students WHERE marks >= 80 AND (age < 20 OR id = 5)

WHERE as an AST (precedence baked into the tree):
AND
  >=
    marks
    80
  OR
    <
      age
      20
    =
      id
      5

Matching rows:
  Ishan
  Dev
```

The `AND` is at the root with `>=` on the left and the parenthesised `OR` on the
right — exactly the precedence we wanted. `Ishan` passes because `88 >= 80` and
`19 < 20`; `Dev` passes because `95 >= 80` and `id = 5`.

## Versus the shunting-yard version in `../dsy`

| | this folder (`queryParsing`) | `dsy` |
|---|---|---|
| Output of parse | AST (tree) | flat postfix list (RPN) |
| Where precedence lives | order of grammar rule calls | precedence number + operator stack |
| Evaluator | recursive tree walk | one-pass int stack |

Real query planners keep a tree because it can carry extra metadata (types, row
estimates, index choices) that a flat RPN list can't. Shunting-yard is leaner and
great as a building block (e.g. an expression inside a `CASE`).
