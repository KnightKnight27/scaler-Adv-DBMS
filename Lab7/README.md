# Lab 7 - Shunting Yard Algorithm and Minimal SQL Parser

24BCS10079 - Piyush Bansal

## Objective

The objective of this lab was to implement:

1. Dijkstra's Shunting Yard Algorithm for expression parsing and evaluation.
2. A minimal SQL SELECT parser that runs queries on records stored in memory.

This helps understand how a database engine processes expressions and simple SQL queries internally.

---

## Files

### 1. shunting_yard.cpp

Implements:

* Infix to Postfix conversion
* Expression evaluation using a stack
* Operator precedence handling
* Parentheses support

### 2. sql_parser.cpp

Implements:

* Basic SELECT query parsing
* Storing records using `vector<Row>`
* Applying a WHERE condition on the rows
* Printing the matching rows

---

## Concepts Used

### Shunting Yard Algorithm

The Shunting Yard Algorithm was given by Edsger Dijkstra to convert an infix
expression into postfix (also called Reverse Polish Notation). Postfix is easy
to evaluate using a single stack.

Example:

```
Infix   : 3+4*2
Postfix : 342*+
Result  : 11
```

The algorithm uses a stack to hold operators while keeping the precedence rules
correct.

### SQL Parser

A small SQL parser was written to see how query processing works. The query is
split into words (tokens), the WHERE condition is read, and every row is checked
against the condition before printing.

Example:

```sql
SELECT * FROM students WHERE age > 20
```

---

## Data Structures Used

* Stack - for the shunting yard algorithm
* Vector + Struct (Row) - for storing and querying the table

```cpp
struct Row {
    int id;
    string name;
    int age;
};
```

---

## Compilation

```bash
g++ shunting_yard.cpp -o shunting
g++ sql_parser.cpp -o parser
```

## Execution

```bash
./shunting
./parser
```

---

## Sample Output

### Shunting Yard

```
Infix   : 3+4*2/(1-5)
Postfix : 342*15-/+
Result  : 1
```

### SQL Parser

```
ID      NAME    AGE
2       Bob     22
4       David   25
```

---

## Conclusion

In this lab Dijkstra's Shunting Yard Algorithm was implemented to convert and
evaluate arithmetic expressions, and a minimal SQL SELECT parser was made to run
a simple query on in-memory rows. This gave a good idea of how databases parse
expressions and execute queries internally.
