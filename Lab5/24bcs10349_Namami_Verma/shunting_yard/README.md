# Dijkstra's Shunting-Yard Expression Evaluator

## Objective

Implement Dijkstra's Shunting-Yard Algorithm in C++ to:

1. Convert infix expressions to postfix notation.
2. Evaluate postfix expressions.
3. Demonstrate operator precedence and associativity handling.

---

## Concepts Covered

- Stack Data Structure
- Infix Expressions
- Postfix Expressions (Reverse Polish Notation)
- Operator Precedence
- Parentheses Handling
- Expression Evaluation

---

## Algorithm Overview

### Infix to Postfix Conversion

The Shunting-Yard algorithm uses:

- Output queue (implemented using vector)
- Operator stack

Rules:

1. Operands are directly added to output.
2. Operators are pushed based on precedence.
3. Parentheses control grouping.
4. Remaining operators are popped at the end.

### Postfix Evaluation

1. Traverse postfix expression.
2. Push operands onto stack.
3. When an operator is found:
   - Pop two operands.
   - Apply operation.
   - Push result back.
4. Final stack value is the answer.

---

## Sample Expression

Input:

3 + 4 * 2 / ( 1 - 5 )

Postfix:

3 4 2 * 1 5 - / +

Result:

1

---

## Compilation

```bash
g++ -std=c++17 shunting_yard.cpp -o shunting_yard
```

## Execution

```bash
./shunting_yard
```

---

## Learning Outcomes

- Understand Dijkstra's Shunting-Yard Algorithm.
- Learn expression parsing techniques.
- Implement stack-based evaluation.
- Handle operator precedence correctly.