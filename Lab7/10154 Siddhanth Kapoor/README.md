# Lab 7: Parsing a SQL WHERE Clause (Two Approaches)

**Name:** Siddhanth Kapoor
**Roll Number:** 10154

Both folders solve the same problem — respect operator precedence inside a SQL
`WHERE` clause (comparisons > `AND` > `OR`, with parentheses) — in two classic
ways, then use the result to filter the same `students` rows.

## `queryParsing/`

A hand-written lexer emits a flat `vector<Token>` (keywords, identifiers, numbers,
comparison ops, parens). A **recursive-descent parser** builds an AST; precedence
is encoded in the grammar (`expr -> term (OR term)*`, `term -> factor (AND factor)*`,
`factor -> '(' expr ')' | cmp`), so comparisons bind tightest and `OR` loosest. The
AST is a single tagged `Expr` struct (`Column` / `Number` / `BinOp`) instead of a
virtual hierarchy, which keeps the executor short and avoids `dynamic_cast` at eval
time. `eval()` walks the tree once per row and prints matching values.

## `dsy/`

**Dijkstra's Shunting-Yard.** Converts the `WHERE` clause from infix to postfix
(RPN) using an operator stack plus a precedence table (comparisons > `AND` > `OR`),
then evaluates the postfix with a single int stack to filter rows. One-pass
tokenizer, then a textbook shunting-yard loop. All operators are left-associative,
so the pop condition is `precedence(top) >= precedence(incoming)`. Uses the same
`students` data as `queryParsing/` so the two approaches are directly comparable.

## How they relate

The parser builds a tree and walks it; shunting-yard flattens to RPN and evaluates
with a stack. A tree carries metadata (types, row estimates, indexes) that a flat
list can't, which is why real planners use one; shunting-yard is lighter and useful
as a building block (e.g. an expression inside a `CASE`). The README in each folder
spells out the trade-offs.

Both print the same result for `marks >= 80 AND (age < 20 OR id = 5)`: **Ishan** and
**Dev**.

## Build & run

```
cd "Lab7/10154 Siddhanth Kapoor/queryParsing" && g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
cd "Lab7/10154 Siddhanth Kapoor/dsy"          && g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```
