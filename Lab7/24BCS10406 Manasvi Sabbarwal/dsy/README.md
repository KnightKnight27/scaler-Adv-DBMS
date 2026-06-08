# Dijkstra's Shunting-Yard

`main.cpp` takes a SQL `WHERE` clause in normal (infix) notation, rewrites it
into postfix (RPN), then walks the postfix once per row to filter.

## Why postfix at all

Reading infix correctly needs precedence rules and parentheses. `2 + 3 * 4`
is `14`, not `20`, because `*` binds tighter than `+`. A SQL engine has the
same problem with `marks >= 80 AND id < 3 OR id > 5`: the parser has to know
that `AND` binds tighter than `OR` and that comparisons bind tighter than both.

Postfix removes that ambiguity. The operator order already tells you what to
do first, so the evaluator is a single left-to-right pass over the tokens
with one stack. No parens, no precedence table, no recursion.

```
Infix    : 2 + 3 * 4
Postfix  : 2 3 4 * +

Evaluate :
  2      -> stack [2]
  3      -> stack [2, 3]
  4      -> stack [2, 3, 4]
  *      -> pop 4 and 3, push 12. stack [2, 12]
  +      -> pop 12 and 2, push 14. stack [14]
```

## Precedence table I used

| Operator | Precedence |
|----------|------------|
| `>` `<` `>=` `<=` `=` | 3 |
| `AND`                 | 2 |
| `OR`                  | 1 |

Higher number means tighter binding. So `marks >= 80 AND id < 3` reads as
`(marks >= 80) AND (id < 3)`.

## The algorithm in one paragraph

Two containers, an output queue and an operator stack. Walk the tokens:

- operand (column name, number): push to output
- operator `o`: while the stack top is an operator with precedence `>= o`,
  pop it to output; then push `o`
- `(`: push to stack
- `)`: pop to output until you hit `(`, discard the `(`
- end of input: pop everything left on the stack to output

## Worked example

Input: `marks >= 80 AND (id < 3 OR id > 5)`

| Token  | Output                                    | Stack          |
|--------|-------------------------------------------|----------------|
| marks  | marks                                     |                |
| >=     | marks                                     | >=             |
| 80     | marks 80                                  | >=             |
| AND    | marks 80 >=                               | AND            |
| (      | marks 80 >=                               | AND (          |
| id     | marks 80 >= id                            | AND (          |
| <      | marks 80 >= id                            | AND ( <        |
| 3      | marks 80 >= id 3                          | AND ( <        |
| OR     | marks 80 >= id 3 <                        | AND ( OR       |
| id     | marks 80 >= id 3 < id                     | AND ( OR       |
| >      | marks 80 >= id 3 < id                     | AND ( OR >     |
| 5      | marks 80 >= id 3 < id 5                   | AND ( OR >     |
| )      | marks 80 >= id 3 < id 5 > OR              | AND            |
| (end)  | marks 80 >= id 3 < id 5 > OR AND          |                |

Final RPN: `marks 80 >= id 3 < id 5 > OR AND`

## Evaluating

Walk the postfix with one int stack. Numbers and column values get pushed.
Operators pop two operands and push the result. Comparison results are `0`
or `1` (treated as booleans for `AND` / `OR`). The last value left on the
stack is the answer for that row.

## Build & run

```
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```
Infix WHERE : marks >= 80 AND (id < 3 OR id > 5)
Postfix     : marks 80 >= id 3 < id 5 > OR AND

Rows matching the WHERE clause:
  Diya (id=2, marks=91)
  Sneha (id=6, marks=95)
```

Diya passes the AND because `91 >= 80` and `2 < 3`. Sneha passes because
`95 >= 80` and `6 > 5`. Everyone else fails one of the two halves.

## How this compares to the parser in `../queryParsing`

Same problem, different shape:

| | `queryParsing/` | this folder |
|---|---|---|
| Output      | AST (tree) | flat postfix list |
| Precedence  | grammar function call order | operator stack + precedence number |
| Evaluator   | recursive tree walk | one-pass stack |

Most real query planners use a tree because it carries metadata (types,
estimated row counts, indexes) that a flat list can't. Shunting-yard is
still useful as a building block, like for a calculator expression inside
a `CASE` statement or a stored procedure.
