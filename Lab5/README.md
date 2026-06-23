# Lab Session 5: Shunting-Yard Algorithm & Minimal SQL SELECT Parser

For this lab, I implemented Dijkstra's Shunting-Yard algorithm so I could parse and evaluate infix arithmetic and boolean expressions. I also built a minimal SQL parser designed to handle SELECT queries over an in-memory `vector<Row>`, which helps simulate how the WHERE clause evaluation would work in a real database.

## What I Built

### Part 1: Expression Evaluator
In SQL, WHERE clauses have infix expressions, like `age > 25 AND salary * 1.1 < 90000`. I used the shunting-yard algorithm to take these infix tokens and convert them into postfix (Reverse Polish Notation or RPN) format. The great thing about RPN is that I can evaluate it cleanly using a simple stack. I built the `tokenize()`, `to_rpn()`, and `eval_rpn()` functions in my code to handle all of this.

### Part 2: Minimal SQL SELECT Parser
I created a simple SQL parser to break down a SELECT query. I defined a `SelectQuery` struct to keep track of columns, the FROM table, the raw WHERE clause, ORDER BY, and LIMIT settings. 

Then I wrote the `execute()` function. It does the following:
- Takes the rows of data and evaluates my WHERE conditions for each row using the RPN evaluator.
- Projects out just the columns I requested.
- Sorts the final result buffer based on the ORDER BY column if there is one.
- Truncates the results if I provided a LIMIT.

## How It All Fits Together

When I tested this, the flow basically looked like:
1. I get the raw SQL string.
2. The tokenizer breaks the query apart.
3. My `parse_select()` builds the query AST.
4. I run `execute()`, which loops through my `vector<Row>` and evaluates the WHERE expression by converting it via my Shunting-Yard implementation.
5. I get the final output result set back.

In a full database, the planner would compile the WHERE expression tree instead of re-parsing it per row, and the `vector<Row>` I used simulates fetching pages from disk like I did in earlier labs. ORDER BY is basically a sort operation on the resulting memory buffer.

## Key Things I Learned
- I found out that the Shunting-Yard algorithm easily handles operator precedence and associativity to determine the final output order, all without any recursion.
- Building the SQL executor really showed me that it's just a sequence of steps: filter the rows (WHERE), project the fields (SELECT), sort the data (ORDER BY), and then truncate (LIMIT).
- I had to use `std::variant` (from C++17) to cleanly handle columns that contain strings along with the ones that are numeric.

To test my work, I just compiled it via:
```bash
g++ -std=c++17 -o sql_parser sql_parser.cpp && ./sql_parser
```
