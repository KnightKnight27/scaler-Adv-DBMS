# Advanced DBMS Lab 7: Dijkstra's Shunting-Yard (SQL WHERE Clause)

> Author: Krritin Keshan (24BCS10122)
> Course: Advanced Database Management Systems
> Language: C++17

## Included Files

| File | Description |
|------|-------------|
| main.cpp | Tokenizer, infix-to-postfix converter, and evaluation driver. |
| makefile | Build script configured with C++17 flags. |
| readme.md | This documentation. |

## Compilation & Execution

make          # Compile
make run      # Compile and run
make clean    # Remove binary

## The Concept

Postfix removes precedence ambiguity. The token order encodes evaluation order,
so evaluation is one left-to-right pass with a single stack — no parens, no
precedence table needed at eval time.

Precedence table (higher = tighter):
  > < >= <= =  →  3
  AND          →  2
  OR           →  1

## The Algorithm

For each token:
- Operand → append to output
- Operator o → pop operators with precedence >= o to output, then push o
- ( → push
- ) → pop to output until (, discard (
- End → pop everything remaining to output

## Worked Example

Input: marks >= 80 AND (age < 20 OR id = 5)
RPN:   marks 80 >= age 20 < id 5 = OR AND

## Sample Output

Infix WHERE : marks >= 80 AND (age < 20 OR id = 5)
Postfix     : marks 80 >= age 20 < id 5 = OR AND

Matching rows:
  Priya (id=1, age=19, marks=88)
  Meera (id=5, age=21, marks=95)

## Comparison with Parser

| Feature | shunting_yard | queryparsing |
|---|---|---|
| Parse output | Flat RPN list | AST (tree) |
| Precedence lives in | Precedence number table | Grammar rule order |
| Evaluator | One-pass integer stack | Recursive tree walk |