# Lab 7 - SQL Query Parsing and Shunting-Yard Evaluation

Name: Aparna Singha  
Roll Number: 24BCS10353

## Objective

The objective of this lab is to simulate small DBMS-style query handling in C++17 without using any real database engine. The work is divided into two parts:

- evaluating SQL-like `WHERE` conditions using Dijkstra's Shunting-Yard algorithm,
- parsing mini `SELECT` queries with an AST-based recursive descent parser.

This lab focuses on query parsing, operator precedence, condition validation, and row filtering on records stored only in memory.

## Folder Structure

```text
Lab7/
└── 24BCS10353_Aparna_Singha/
    ├── README.md
    ├── dijkstra-shunting/
    │   └── main.cpp
    └── queryParsing/
        └── main.cpp
```

## Part 1: Dijkstra Shunting-Yard Condition Evaluator

The first program accepts SQL-like `WHERE` conditions such as:

```sql
rating >= 4 AND year > 2015
```

It tokenizes the condition, validates the field names and operators, converts the infix expression to postfix form, and then evaluates the postfix expression row by row on a Book dataset.

Supported features:

- numeric comparisons on `bookId`, `year`, `pages`, and `rating`,
- string comparisons on `title`, `author`, and `genre`,
- logical operators `AND` and `OR`,
- parentheses for grouping,
- postfix conversion using operator precedence,
- formatted output showing tokens, postfix form, and matching rows.

## Part 2: SQL Query Parser

The second program parses mini SQL queries like:

```sql
SELECT title, author FROM books WHERE rating >= 4 AND year > 2015
```

This part supports:

- `SELECT`,
- column lists,
- `SELECT *`,
- `FROM books`,
- optional `WHERE`,
- recursive descent parsing for conditions,
- AST creation with `std::unique_ptr`,
- result filtering and formatted row output.

The `WHERE` condition parser is more structured than the first part. Instead of directly converting to postfix, it builds an AST and evaluates the tree for each row.

## Features Implemented

- C++17 implementation with standard headers only
- original in-memory Book dataset
- tokenization for both conditions and full queries
- field and table validation
- comparison operator validation
- mismatched parentheses detection
- malformed expression detection
- readable tabular output for results
- safe handling of invalid queries without program crashes

## Dataset Used

Both programs use the same in-memory Book/Library dataset with the following fields:

- `bookId`
- `title`
- `author`
- `genre`
- `year`
- `pages`
- `rating`

Sample genres in the dataset include Fiction, Mystery, Science, History, Technology, and Nonfiction. No real DBMS is used here. All records are stored inside C++ vectors only for lab demonstration.

## Sample Conditions

The Shunting-Yard program demonstrates conditions such as:

```sql
rating >= 4 AND year > 2015
genre = Fiction OR genre = Mystery
( genre = Fiction OR genre = Mystery ) AND pages < 400
rating > 5 AND year < 2000
```

An extra invalid condition is also included to show error reporting for mismatched parentheses.

## Sample SQL Queries

The SQL parser demonstrates queries such as:

```sql
SELECT title, author FROM books WHERE rating >= 4 AND year > 2015
SELECT * FROM books WHERE genre = Fiction OR pages < 250
SELECT title, genre, rating FROM books WHERE ( genre = Mystery OR genre = Fiction ) AND rating >= 4
SELECT title, year, rating FROM books
```

One invalid query is also included:

```sql
SELECT title, price FROM books WHERE rating >= 4
```

This is handled gracefully with a clear error message because `price` is not a valid column in the Book dataset.

## Compilation and Running Instructions

Use the following commands exactly:

```bash
cd Lab7/24BCS10353_Aparna_Singha

g++ -std=c++17 -Wall -Wextra dijkstra-shunting/main.cpp -o dijkstra_demo
./dijkstra_demo

g++ -std=c++17 -Wall -Wextra queryParsing/main.cpp -o query_parser
./query_parser
```

## Explanation of Shunting-Yard Algorithm

Dijkstra's Shunting-Yard algorithm is used to convert infix expressions into postfix form. In infix notation, operators appear between operands, which is easy for humans to read but slightly harder to evaluate directly when precedence and parentheses are involved.

In this lab:

- comparison expressions are treated as condition units,
- `AND` has higher precedence than `OR`,
- parentheses override normal precedence,
- the infix condition is converted to postfix,
- the postfix condition is evaluated using a stack.

This makes the evaluation order explicit and easy to execute row by row.

## Explanation of Query Parsing

The query parser uses a recursive descent approach for SQL-like statements and `WHERE` conditions.

The statement parser checks:

- whether the query starts with `SELECT`,
- whether the selected columns are valid,
- whether `FROM` is present,
- whether the table name is `books`,
- whether a `WHERE` clause exists.

For the `WHERE` clause, the parser builds an AST using these logical levels:

- `parseExpression()`
- `parseOr()`
- `parseAnd()`
- `parsePrimary()`
- `parseComparison()`

This ensures the correct precedence:

1. comparison operators
2. `AND`
3. `OR`

Parentheses are handled through recursive parsing.

## Error Handling

The programs report clear messages for cases like:

- unknown field names
- unknown selected columns
- unknown table name
- missing `FROM`
- unsupported operators on string fields
- missing comparison values
- malformed logical expressions
- mismatched parentheses

Errors are caught and printed so the demo continues running even if one condition or query is invalid.

## Conclusion

This lab demonstrates how basic query processing ideas from DBMS can be implemented manually in C++17. Even without using any database library, the programs show how tokenization, precedence handling, postfix evaluation, recursive parsing, and AST evaluation work together to process SQL-like input on an in-memory dataset.
