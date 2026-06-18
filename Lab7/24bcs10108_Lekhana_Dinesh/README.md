# Lab 7 - SQL Query Parsing and Shunting-Yard Evaluation

Name: Lekhana Dinesh  
Roll No: 24BCS10108

## 1. Objective

This lab demonstrates two small database-related programs written in C++17:

- evaluating SQL-like `WHERE` conditions with Dijkstra's Shunting-Yard algorithm
- parsing simple `SELECT` queries and showing matching rows from an in-memory product dataset

The goal is to understand tokenization, operator precedence, postfix conversion, and manual query parsing without using a real database engine.

## 2. Folder structure

```text
Lab7/24bcs10108_Lekhana_Dinesh/
├── README.md
├── dijkstra-shunting/
│   └── main.cpp
└── queryParsing/
    └── main.cpp
```

## 3. Program 1: Dijkstra Shunting-Yard WHERE evaluator

File: `dijkstra-shunting/main.cpp`

This program:

- stores a product dataset in memory using a `Product` structure
- tokenizes a SQL-like condition string
- converts the infix condition to postfix form using the Shunting-Yard method
- evaluates the postfix expression for every product row
- prints only the rows that satisfy the condition

Supported examples:

- `price > 500 AND stock > 10`
- `category = Electronics OR price < 100`
- `( category = Electronics OR category = Furniture ) AND stock >= 5`
- `price <= 200 OR stock = 0`

## 4. Program 2: Simple SELECT query parser

File: `queryParsing/main.cpp`

This program:

- tokenizes a SQL query
- parses the selected columns after `SELECT`
- checks the table name after `FROM`
- reads an optional `WHERE` clause
- filters product rows using simple comparisons joined by `AND` or `OR`
- prints only the requested columns

Supported query shapes:

- `SELECT name,price FROM products WHERE price > 500`
- `SELECT * FROM products WHERE category = Electronics AND stock > 5`
- `SELECT name,category,stock FROM products WHERE stock <= 10 OR price < 100`
- `SELECT id,name FROM products`

## 5. Supported condition operators

Both programs support these comparison operators:

- `=`
- `!=`
- `<`
- `<=`
- `>`
- `>=`

Logical operators used in the conditions:

- `AND`
- `OR`

## 6. Operator precedence table

| Operator type | Operators | Priority |
| --- | --- | --- |
| Comparison | `= != < <= > >=` | Highest |
| Logical | `AND` | Middle |
| Logical | `OR` | Lowest |

Parentheses are used in Program 1 to override the normal precedence order.

## 7. Example infix to postfix conversion using the Product dataset

Example condition:

```text
( category = Electronics OR category = Furniture ) AND stock >= 5
```

Token sequence:

```text
( category = Electronics OR category = Furniture ) AND stock >= 5
```

Postfix form:

```text
category Electronics = category Furniture = OR stock 5 >= AND
```

When this postfix expression is checked against the product rows, the program prints the products whose category is either `Electronics` or `Furniture` and whose stock is at least `5`.

## 8. Compile and run instructions

For Program 1:

```bash
cd Lab7/24bcs10108_Lekhana_Dinesh/dijkstra-shunting
g++ -std=c++17 -Wall -Wextra main.cpp -o dijkstra_eval
./dijkstra_eval
```

Windows PowerShell:

```powershell
cd Lab7/24bcs10108_Lekhana_Dinesh/dijkstra-shunting
g++ -std=c++17 -Wall -Wextra main.cpp -o dijkstra_eval
.\dijkstra_eval.exe
```

For Program 2:

```bash
cd Lab7/24bcs10108_Lekhana_Dinesh/queryParsing
g++ -std=c++17 -Wall -Wextra main.cpp -o query_parser
./query_parser
```

Windows PowerShell:

```powershell
cd Lab7/24bcs10108_Lekhana_Dinesh/queryParsing
g++ -std=c++17 -Wall -Wextra main.cpp -o query_parser
.\query_parser.exe
```

## 9. Sample output snippets

Program 1 sample:

```text
Condition: price > 500 AND stock > 10
Tokens : price > 500 AND stock > 10
Postfix: price 500 > stock 10 > AND
Matched rows:
ID   Name                Category       Price     Stock
```

Program 2 sample:

```text
Query: SELECT name,price FROM products WHERE price > 500
Tokens         : SELECT name , price FROM products WHERE price > 500
Selected cols  : name, price
Table          : products
WHERE present  : Yes
Result rows:
```

## 10. Error handling

The programs use `std::runtime_error` to report simple input errors such as:

- unknown field names
- unknown selected columns
- invalid comparison operators
- malformed conditions
- mismatched parentheses
- missing `SELECT`
- missing `FROM`
- invalid table name
- invalid `WHERE` clause

## 11. Learning outcome

By completing this lab, I practiced:

- converting infix expressions to postfix notation
- evaluating postfix expressions with stacks
- handling operator precedence in boolean conditions
- writing a basic SQL-style parser manually
- filtering structured records without using a database library

## 12. Originality note

This Lab 7 submission was implemented independently. It follows the required topic of SQL query parsing and Shunting-Yard expression evaluation, but uses an original product dataset, original examples, separate code organization, and original documentation wording.
