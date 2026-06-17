# Query Parsing

Notes from writing `main.cpp`, a tiny SQL front-end that takes a string like

```
SELECT name FROM students WHERE marks >= 80 AND id < 6
```

and returns the rows that match.

## Pipeline

```
SQL text
   │
   ▼
  lex()              -> tokens
   │
   ▼
  Parser.parseSelect -> Select { column, table, WHERE expression tree }
   │
   ▼
  run()              -> walk the tree row-by-row, print matches
```

Three phases, same shape as a real database, just with everything chopped down
to the parts the lab cares about.

## 1. Lexing

The lexer scans the raw string left-to-right and emits a flat `vector<Token>`.
A token is the smallest meaningful unit: a keyword, an identifier, a number, an
operator, or a parenthesis. Whitespace is dropped.

Why bother with a separate lex step? The parser becomes much simpler if it
deals with `Tok::Where` instead of "five characters that happen to spell
W-H-E-R-E". Two-character operators like `>=` are also easier to handle in
the lexer than in the parser.

Example:

```
SELECT name FROM students WHERE marks >= 80
   ↓
[SELECT] [name] [FROM] [students] [WHERE] [marks] [>=] [80] [END]
```

## 2. Parsing → AST

The parser is **recursive descent**: one function per grammar rule, and the
call graph reflects operator precedence. The grammar I implemented:

```
select  := SELECT IDENT FROM IDENT WHERE expr
expr    := term  ( OR  term  )*       -- OR binds loosest
term    := factor ( AND factor )*     -- AND tighter
factor  := '(' expr ')' | cmp
cmp     := IDENT (>|<|>=|<=|=) NUMBER -- comparisons tightest
```

Because `parseExpr` calls `parseTerm` which calls `parseFactor`, the parser
naturally evaluates a comparison first, then `AND` groups of comparisons,
then `OR` groups of those. Parentheses short-circuit back to the top.

The output is a tiny tree of `Expr` nodes. I went with **one struct + a `Node`
enum tag** instead of a virtual class hierarchy. It's a couple of fields, no
`dynamic_cast` at eval time, and the tree is easier to read in the debugger.

For `marks >= 80 AND id < 6` the tree looks like:

```
        AND
       /   \
     >=     <
    /  \   / \
 marks 80 id  6
```

## 3. Executing

`run()` walks the tree once per row. `matches()` is two lines for `AND`/`OR`
plus a small switch over comparison operators. Column names on the left of a
comparison resolve through `columnValue()`; the right side is always a
numeric literal in this lab.

The data table is six `Student` rows hard-coded in `main()`. The query in
`main()` is:

```
SELECT name FROM students WHERE marks >= 80 AND id < 6
```

Expected output:

```
Result of: SELECT name FROM students WHERE ...
  Diya
  Meera
```

![output](../output.png)

Diya (id 2, marks 91) and Meera (id 4, marks 88) both clear the bar. Sneha
has 95 marks but `id = 6` is excluded by `id < 6`, which is exactly the kind
of thing the AND node is supposed to enforce.

## Build & run

```
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

## What I took away

- Precedence isn't a runtime check. It's encoded in *which parser function
  calls which*. That's why the grammar layering matters.
- The AST is what real query planners pass around. Joins, indexes, projection
  pushdown. They all start by rewriting this tree.
- Keeping the node type as a single tagged struct made the executor short.
  A bigger language would want a real visitor, but for one lab the tagged
  union is cleaner than a hierarchy.
