# Dijkstra's Shunting-Yard Expression Evaluator

## Included Files

| File | Description |
|------|-------------|
| `main.cpp` | Core implementation containing the Tokenizer, infix-to-postfix converter, and evaluation driver testing it against expressions. |
| `makefile` | Build script configured with standard C++17 compiler flags. |
| `readme.md` | This technical documentation. |

## Overview

This implementation demonstrates **Dijkstra's Shunting-Yard algorithm** for converting infix mathematical expressions to postfix (Reverse Polish Notation) and evaluating them.

## Features

- **Tokenizer**: Parses infix expressions into tokens (numbers, operators, parentheses).
- **Infix-to-Postfix Converter**: Uses the Shunting-Yard algorithm with an operator precedence stack.
- **Evaluator**: Evaluates postfix expressions using a value stack.

## Supported Operators

- `+` (addition)
- `-` (subtraction)
- `*` (multiplication)
- `/` (division)
- `^` (exponentiation)

## Build

```bash
cd shunting_yard
make
```

## Run

```bash
./shunting_yard
```

## Example Output

```
Expression: 3 + 4 * 2 = 11
Expression: ( 3 + 4 ) * 2 = 14
Expression: 10 / 2 + 3 = 8
Expression: 2 ^ 3 + 5 = 13
Expression: ( ( 15 ) ) + 5 * 2 = 25
```

## Algorithm Explanation

1. **Tokenization**: Break the input string into tokens.
2. **Shunting-Yard**: Move operators to a stack based on precedence and associativity.
3. **Evaluation**: Use a stack to compute the postfix expression left-to-right.

## Time Complexity

- Tokenization: O(n)
- Conversion to postfix: O(n)
- Evaluation: O(n)

**Overall**: O(n) where n is the length of the expression.
