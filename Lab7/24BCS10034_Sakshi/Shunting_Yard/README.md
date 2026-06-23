# Part 1: Shunting-Yard Algorithm (Expression Evaluator)

This directory contains the implementation of **Dijkstra's Shunting-Yard Algorithm**, used to evaluate infix arithmetic and boolean expressions. This algorithm is the foundation for parsing and evaluating `WHERE` clauses in SQL engines.

## Files
- `shunting_yard.cpp`: A standalone C++ implementation of the Shunting-Yard algorithm.

## Features
- **Tokenization:** Converts an infix string expression into tokens (numbers, identifiers, operators, parentheses).
- **Infix to Postfix (RPN):** Converts the tokenized expression into Reverse Polish Notation (RPN) using an operator stack.
- **Evaluation:** Evaluates the resulting RPN expression against a map of variables, effectively executing the boolean/arithmetic logic.

## How to Compile & Run
To run the standalone algorithm, you need a C++17 compliant compiler:

```bash
g++ -std=c++17 -o shunting_yard shunting_yard.cpp
./shunting_yard
```

### Expected Output
The program evaluates the hardcoded expression `age * 2 + salary / 1000 > 100` and displays its tokenized RPN equivalent, as well as the final boolean result (1.0 for true, 0.0 for false).
