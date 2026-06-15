# Lab 7 - Simple SQL Query Parser

## Student Details
- Name: Harshit Tiwari
- Roll Number: 24BCS10277

## Aim
Build a small SQL parser in C++ that can read a `SELECT ... FROM ... WHERE ...` query and evaluate the `WHERE` condition on an in-memory table.

## Files
- `main.cpp`: Tokenizer, parser, expression tree, evaluator, and sample driver.

## Supported Query Shape

```sql
SELECT column_list FROM employees WHERE condition;
```

Examples:

```sql
SELECT name, department FROM employees WHERE age >= 30 AND salary > 70000;
SELECT * FROM employees WHERE department = 'Engineering' OR age < 26;
SELECT id, name FROM employees WHERE (salary >= 80000 AND age < 40) OR department = 'HR';
```

## Features
- Tokenizes SQL keywords, identifiers, numbers, quoted strings, operators, commas, and parentheses.
- Parses selected columns and table name.
- Parses `WHERE` expressions with comparison operators, `AND`, `OR`, and nested parentheses.
- Builds an expression tree using recursive-descent parsing.
- Evaluates the parsed condition against sample rows and prints matching records.

## Compile And Run

```powershell
g++ -std=c++17 lab7/main.cpp -o lab7/sql_parser
.\lab7\sql_parser.exe
```

## Notes
The parser uses precedence-aware recursive descent:
- Comparisons are parsed first.
- `AND` binds tighter than `OR`.
- Parentheses can override the default precedence.
