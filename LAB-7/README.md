# Lab 7 - SQL Query Processor

## Student Details
- Name: Rudray Mehra
- Roll Number: 24BCS10425

## Objective
Build the front of a minimal query engine in two parts:
1. Implement Dijkstra's **Shunting-Yard** algorithm to convert an infix WHERE
   expression into postfix (RPN), then evaluate the RPN with a stack.
2. Build a minimal **SQL SELECT parser + executor** that runs queries against an
   already-fetched `vector<Row>` in memory.

## Files
- `sql_query_processor.cpp`: Complete C++17 implementation — tokenizer,
  shunting-yard infix→RPN conversion, RPN evaluator, a `SELECT` parser producing
  a `SelectQuery`, and an executor (filter → project → sort → limit). Includes a
  shunting-yard demo plus two sample queries over a student table.

## Compile and Run

```bash
g++ -std=c++17 LAB-7/sql_query_processor.cpp -o LAB-7/sql_query_processor
./LAB-7/sql_query_processor
```

## How the pieces connect
```
SQL string
   |  tokenize()
Lexer / Tokenizer
   |  parse_select()   -> SelectQuery (a tiny AST)
Parser
   |  execute()
Executor   ── WHERE evaluated per row via to_rpn() + eval_rpn() (shunting-yard)
   |
Result set (vector<Row>)
```

## Key Concepts
- **Shunting-Yard** converts infix to RPN in O(n) using one operator stack; no
  recursion needed. Operator **precedence** and **associativity** decide output
  order (e.g. `*` binds tighter than `+`; `^` is right-associative).
- A minimal SQL executor is just four steps: filter (WHERE) → project (SELECT
  columns) → sort (ORDER BY) → truncate (LIMIT).
- Columns are stored as `std::variant<double, std::string>` so both numeric and
  text values are supported; WHERE evaluation uses a numeric view of each column.

## Notes
This lab models the query *front-end*. In a real database the WHERE expression
is compiled into an expression tree once by the planner (not re-parsed per row),
and the planner would choose between an index probe and a full scan — the
`vector<Row>` here stands in for rows already fetched from the storage layer in
the earlier labs.
