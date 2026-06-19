# Lab 7 - Dijkstra's Shunting Yard Algorithm and Minimal SQL Parser

## Objective

The objective of this lab was to implement:

1. Dijkstra's Shunting Yard Algorithm for expression parsing and evaluation.
2. A minimal SQL SELECT parser operating on a collection of rows stored in memory.

This assignment demonstrates how databases and query engines process expressions and simple SQL statements internally.

---

## Files

### 1. shunting_yard.cpp

Implements:

* Infix to Postfix conversion
* Expression evaluation using stacks
* Operator precedence handling
* Parentheses support

### 2. sql_parser.cpp

Implements:

* Basic SQL SELECT query parsing
* Storage of records using `vector<Row>`
* Query execution on in-memory data
* Support for simple SELECT statements

---

## Concepts Used

### Shunting Yard Algorithm

The Shunting Yard Algorithm was developed by Edsger Dijkstra for converting infix expressions into postfix expressions.

Example:

Infix:

```text
3 + 4 * 2
```

Postfix:

```text
342*+
```

Result:

```text
11
```

The algorithm uses a stack to manage operators while preserving precedence and associativity rules.

---

### SQL Parser

A minimal SQL parser was implemented to understand how database query processing works.

Example Queries:

```sql
SELECT * FROM students
```

```sql
SELECT name FROM students
```

The parser reads the query, identifies the selected columns, and displays matching data from a vector of rows.

---

## Data Structure Used

### For Shunting Yard

* Stack

Used for:

* Operator storage
* Postfix generation
* Expression evaluation

### For SQL Parser

* Vector
* Struct (Row)

Example:

```cpp
struct Row {
    int id;
    string name;
    int age;
};
```

Records are stored inside a vector and queried through the parser.

---

## Compilation

Compile the programs using:

```bash
g++ shunting_yard.cpp -o shunting
g++ sql_parser.cpp -o parser
```

---

## Execution

Run the programs using:

```bash
./shunting
```

```bash
./parser
```

---

## Sample Output

### Shunting Yard

```text
Infix: 3+4*2
Postfix: 342*+
Result: 11
```

### SQL Parser

Input:

```sql
SELECT * FROM students
```

Output:

```text
ID      NAME      AGE
1       Alice     20
2       Bob       22
3       Charlie   19
```

---

## Applications

* Database query processing
* Expression evaluation engines
* Compilers and interpreters
* Query optimization systems
* Mathematical expression parsing

---

## Conclusion

In this lab, Dijkstra's Shunting Yard Algorithm was implemented to convert and evaluate arithmetic expressions. A minimal SQL SELECT parser was also developed to simulate basic query execution on in-memory records. The assignment provided insight into how expression parsing and query processing are handled internally in database systems.
