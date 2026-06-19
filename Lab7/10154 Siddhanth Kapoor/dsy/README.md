# Dijkstra's Shunting-Yard

`main.cpp` takes a SQL `WHERE` clause in normal (infix) notation, rewrites it to
postfix (RPN) with Dijkstra's shunting-yard algorithm, then walks the postfix
once per row to filter. Build & run:

```
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

## Why postfix

Reading infix correctly needs precedence rules and parentheses: `2 + 3 * 4` is
`14`, not `20`, because `*` binds tighter than `+`. A `WHERE` clause has the same
issue — `marks >= 80 AND age < 20 OR id = 5` only means the right thing if you
know comparisons bind tighter than `AND`, and `AND` tighter than `OR`.

Postfix removes the ambiguity. The token order already encodes what to do first,
so evaluation is one left-to-right pass with a single stack — no parens, no
precedence table, no recursion at eval time.

## Precedence table

| Operator | Precedence |
|----------|------------|
| `>` `<` `>=` `<=` `=` | 3 |
| `AND` | 2 |
| `OR`  | 1 |

Higher = binds tighter. All operators are left-associative, so the shunting-yard
pop condition is `precedence(top) >= precedence(incoming)`.

## The algorithm

Two containers — an output list and an operator stack. For each token:

- operand (column or number): append to output
- operator `o`: while the stack top is an operator with precedence `>= o`, pop it
  to output; then push `o`
- `(`: push
- `)`: pop to output until `(`, discard the `(`
- end: pop everything left to output

## Worked example

Input: `marks >= 80 AND (age < 20 OR id = 5)`

| Token | Output so far | Stack |
|-------|---------------|-------|
| marks | marks | |
| >= | marks | >= |
| 80 | marks 80 | >= |
| AND | marks 80 >= | AND |
| ( | marks 80 >= | AND ( |
| age | marks 80 >= age | AND ( |
| < | marks 80 >= age | AND ( < |
| 20 | marks 80 >= age 20 | AND ( < |
| OR | marks 80 >= age 20 < | AND ( OR |
| id | marks 80 >= age 20 < id | AND ( OR |
| = | marks 80 >= age 20 < id | AND ( OR = |
| 5 | marks 80 >= age 20 < id 5 | AND ( OR = |
| ) | marks 80 >= age 20 < id 5 = OR | AND |
| end | marks 80 >= age 20 < id 5 = OR AND | |

Final RPN: `marks 80 >= age 20 < id 5 = OR AND`

## Evaluating

Walk the postfix with one int stack. Operands push their value (numbers as-is,
columns looked up from the row). Each operator pops two values and pushes the
result; comparison results are `0`/`1` and feed straight into `AND`/`OR`. The last
value on the stack is the answer for that row.

## Output

```
Infix WHERE : marks >= 80 AND (age < 20 OR id = 5)
Postfix     : marks 80 >= age 20 < id 5 = OR AND

Matching rows:
  Ishan (id=1, age=19, marks=88)
  Dev (id=5, age=21, marks=95)
```

`Ishan` passes (`88 >= 80` and `19 < 20`); `Dev` passes (`95 >= 80` and `id = 5`).
The same two rows the AST parser in `../queryParsing` returns.

## Versus the parser in `../queryParsing`

Same problem, different shape: the parser builds a tree and recurses over it;
shunting-yard flattens to RPN and evaluates with a stack. The parser's tree can
carry metadata a flat list can't, which is why most real planners use one; the
shunting-yard is lighter and handy for standalone expression evaluation.
