# Lab 5: Shunting-Yard Algorithm & SQL SELECT Parser

**Student:** Pulasari Jai  
**Roll No:** 24BCS10656  
**Date:** June 23, 2026

---

## Overview

This lab implements two critical components of query processing:
1. **Dijkstra's Shunting-Yard Algorithm** - Converts infix expressions to postfix (RPN) and evaluates them with correct operator precedence
2. **Minimal SQL SELECT Parser** - Parses and executes SELECT queries over in-memory data

Together, these demonstrate the foundation of SQL query execution engines used in all relational databases.

---

## Objectives

1. ✅ Implement Shunting-Yard algorithm for infix → RPN conversion
2. ✅ Evaluate RPN expressions with operator precedence and associativity
3. ✅ Parse SQL SELECT statements with WHERE, ORDER BY, LIMIT clauses
4. ✅ Execute queries against vector<Row> in memory
5. ✅ Understand query execution pipeline (filter → project → sort → limit)

---

## Directory Structure

```
lab_5/
├── README.md       # This file
├── sql_parser.cpp  # Complete implementation
├── compile.sh      # Build script
└── sql_parser      # Compiled binary
```

---

## Part 1: Shunting-Yard Algorithm

### Background

SQL WHERE clauses contain infix expressions like `age > 25 AND salary * 1.1 < 90000`. The Shunting-Yard algorithm converts infix notation to Reverse Polish Notation (RPN), which can be evaluated efficiently with a stack.

### Algorithm Steps

**Input:** `3 + 4 * 2`

1. **Tokenize:** `[3, +, 4, *, 2]`
2. **Convert to RPN:**
   ```
   Token: 3      → Output: [3]
   Token: +      → Operator stack: [+]
   Token: 4      → Output: [3, 4]
   Token: *      → Operator stack: [+, *]  (* higher precedence)
   Token: 2      → Output: [3, 4, 2]
   End           → Pop all: [3, 4, 2, *, +]
   ```
3. **Evaluate RPN:**
   ```
   Stack: [3]
   Stack: [3, 4]
   Stack: [3, 4, 2]
   * → 4 * 2 = 8, Stack: [3, 8]
   + → 3 + 8 = 11, Stack: [11]
   Result: 11
   ```


### Operator Precedence Table

| Operator | Precedence | Associativity | Example |
|----------|------------|---------------|---------|
| `||` (OR) | 1 | Left | `a || b || c` |
| `&&` (AND) | 2 | Left | `a && b && c` |
| `=`, `!=` | 3 | Left | `a = b` |
| `<`, `>`, `<=`, `>=` | 4 | Left | `a < b` |
| `+`, `-` | 5 | Left | `a + b - c` |
| `*`, `/` | 6 | Left | `a * b / c` |
| `^` (exponentiation) | 7 | **Right** | `2 ^ 3 ^ 2 = 512` |

**Right-associative:** `2 ^ 3 ^ 2` evaluates as `2 ^ (3 ^ 2)` = `2 ^ 9` = 512, not `(2 ^ 3) ^ 2` = 64

### Testing Results

**Test 1:** `age * 2 + salary / 1000 > 100`
```
Tokens: age * 2 + salary / 1000 > 100
RPN: age 2 * salary 1000 / + 100 >
Variables: age=30, salary=50000
Result: 1 (true)
```
✅ Complex expression with multiple operators evaluated correctly

**Test 2:** `3 + 4 * 2`
```
RPN: 3 4 2 * +
Result: 11
```
✅ Operator precedence: multiplication before addition

**Test 3:** `(3 + 4) * 2`
```
RPN: 3 4 + 2 *
Result: 14
```
✅ Parentheses override precedence

**Test 4:** `2 ^ 3 ^ 2` (right-associative)
```
RPN: 2 3 2 ^ ^
Result: 512 (not 64!)
```
✅ Right-associativity handled correctly

**Test 5:** `gpa >= 3.5 && age < 25`
```
RPN: gpa 3.5 >= age 25 < &&
Variables: gpa=3.8, age=30
Result: 0 (false)
```
✅ Boolean expressions work correctly

---

## Part 2: SQL SELECT Parser

### Supported SQL Syntax

```sql
SELECT column1, column2, ... | *
FROM table
[WHERE condition]
[ORDER BY column [ASC|DESC]]
[LIMIT n]
```

### Query Execution Pipeline

```
┌──────────────┐
│   SQL Text   │
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  1. PARSE    │ → Split into components (SELECT, FROM, WHERE, ...)
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  2. FILTER   │ → Evaluate WHERE clause (using Shunting-Yard)
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  3. PROJECT  │ → Select specified columns or *
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  4. SORT     │ → ORDER BY (std::sort with comparator)
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  5. LIMIT    │ → Truncate result set
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  Result Rows │
└──────────────┘
```


### Testing Results

**Sample Data:** 5 students with id, name, age, gpa

**Query 1:** `SELECT * FROM students`
```
Result: All 5 rows with all columns
(5 rows)
```
✅ Basic SELECT * working

**Query 2:** `SELECT name, gpa FROM students WHERE gpa > 3.0`
```
Result:
name=Alice  gpa=3.8
name=Carol  gpa=3.5
name=Dave   gpa=3.1
name=Eve    gpa=3.9
(4 rows)
```
✅ WHERE clause filtering + column projection

**Query 3:** `SELECT name, age, gpa FROM students WHERE gpa > 3.5 ORDER BY gpa DESC`
```
Result:
name=Eve    age=23  gpa=3.9
name=Alice  age=22  gpa=3.8
(2 rows)
```
✅ WHERE + ORDER BY DESC working correctly

**Query 4:** `SELECT name, gpa FROM students WHERE age >= 22 && age <= 26 ORDER BY gpa DESC LIMIT 2`
```
Result:
name=Eve    gpa=3.9
name=Alice  gpa=3.8
(2 rows)
```
✅ Complex WHERE with AND operator + ORDER BY + LIMIT

**Query 5:** `SELECT * FROM students WHERE gpa >= 3.5 && age < 25`
```
Result:
gpa=3.8  age=22  name=Alice  id=1
gpa=3.5  age=21  name=Carol  id=3
gpa=3.9  age=23  name=Eve    id=5
(3 rows)
```
✅ Multiple conditions with logical AND

---

## Implementation Details

### Data Structures

**Value type:**
```cpp
using Value = std::variant<double, std::string>;
```
Supports both numeric and string column values.

**Row type:**
```cpp
struct Row {
    std::unordered_map<std::string, Value> cols;
};
```
Flexible schema - each row is a map of column names to values.

**SelectQuery type:**
```cpp
struct SelectQuery {
    std::vector<std::string> columns;   // Projection
    std::string from;                   // Table name
    std::string where_raw;              // WHERE clause
    std::string order_by;               // Sort column
    bool order_asc = true;              // Sort direction
    int limit = -1;                     // Result limit
};
```

### Key Functions

**1. tokenize(expr)** - O(n)
```cpp
Input:  "age * 2 + 10"
Output: ["age", "*", "2", "+", "10"]
```

**2. to_rpn(tokens)** - O(n)
```cpp
Input:  ["age", "*", "2", "+", "10"]
Output: ["age", "2", "*", "10", "+"]
```

**3. eval_rpn(rpn, vars)** - O(n)
```cpp
Input:  ["age", "2", "*", "10", "+"], {age: 30}
Output: 70.0
```

**4. parse_select(sql)** - O(n)
```cpp
Input:  "SELECT name FROM students WHERE age > 20"
Output: SelectQuery{columns: ["name"], from: "students", where_raw: "age > 20"}
```

**5. execute(query, data)** - O(n * m) where m = WHERE complexity
```cpp
Input:  SelectQuery, vector<Row>
Output: Filtered, projected, sorted, limited result rows
```


---

## Building and Running

### Compile

```bash
chmod +x compile.sh
./compile.sh
```

Or manually:
```bash
g++ -std=c++17 -O2 -Wall -Wextra -o sql_parser sql_parser.cpp
```

### Run

```bash
./sql_parser
```

---

## How This Connects to Real Databases

### Query Processing in PostgreSQL

```
┌────────────────────────────────────────────┐
│  SQL Text                                  │
│  "SELECT name FROM students WHERE age>20"  │
└─────────────────┬──────────────────────────┘
                  │
                  ↓
┌─────────────────────────────────────────────┐
│  1. LEXER / TOKENIZER                       │  ← tokenize()
│     Break into tokens                       │
└─────────────────┬───────────────────────────┘
                  │
                  ↓
┌─────────────────────────────────────────────┐
│  2. PARSER                                  │  ← parse_select()
│     Build AST (Abstract Syntax Tree)        │
└─────────────────┬───────────────────────────┘
                  │
                  ↓
┌─────────────────────────────────────────────┐
│  3. PLANNER                                 │  (Not in our lab)
│     Choose index vs full scan               │
│     Estimate costs                          │
└─────────────────┬───────────────────────────┘
                  │
                  ↓
┌─────────────────────────────────────────────┐
│  4. EXECUTOR                                │  ← execute()
│     - Scan relation (table/index)           │
│     - Evaluate WHERE (expression tree)      │  ← Shunting-Yard
│     - Project columns                       │
│     - Sort if needed                        │
│     - Limit results                         │
└─────────────────┬───────────────────────────┘
                  │
                  ↓
┌────────────────────────────────────────────┐
│  Result Set                                │
└────────────────────────────────────────────┘
```

### Expression Trees in Real Databases

In production databases, WHERE clause expressions are compiled into **expression trees**, not re-parsed per row:

```
WHERE age > 20 AND gpa >= 3.5

Expression Tree:
       AND
      /   \
    >       >=
   / \     /  \
 age 20  gpa  3.5
```

**Our implementation:** Compiles to RPN once, then evaluates per row  
**Real databases:** Compile to expression tree with JIT compilation

---

## Performance Analysis

### Time Complexity

| Operation | Complexity | Notes |
|-----------|------------|-------|
| tokenize() | O(n) | n = expression length |
| to_rpn() | O(n) | Single pass with stack |
| eval_rpn() | O(n) | Stack operations |
| parse_select() | O(n) | n = SQL string length |
| execute() | O(m * w) | m = rows, w = WHERE complexity |
| ORDER BY | O(m log m) | std::sort |

### Space Complexity

| Structure | Space | Notes |
|-----------|-------|-------|
| Tokens | O(n) | Number of tokens |
| RPN | O(n) | Same size as tokens |
| Operator stack | O(n) | Worst case: all operators |
| Evaluation stack | O(n) | Worst case: all operands |
| Result rows | O(m) | Filtered result size |

---

## Key Concepts Demonstrated

### 1. Operator Precedence

```
3 + 4 * 2 = 11  (not 14)

Why? Because * has higher precedence than +
RPN: 3 4 2 * +
```

### 2. Associativity

```
2 ^ 3 ^ 2 = 512  (not 64)

Why? ^ is right-associative
Evaluates as: 2 ^ (3 ^ 2) = 2 ^ 9 = 512
RPN: 2 3 2 ^ ^
```

### 3. Parentheses Override

```
(3 + 4) * 2 = 14

Parentheses force addition first
RPN: 3 4 + 2 *
```

### 4. Short-Circuit Evaluation (Not Implemented)

Real databases use short-circuit evaluation:
```sql
WHERE expensive_function(x) AND cheap_check(y)
```
If `cheap_check(y)` is false, `expensive_function(x)` is never called!


### 5. Query Execution Order

SQL is **declarative** - you specify WHAT you want, not HOW to get it.

**Logical order (what you write):**
```sql
SELECT name          -- 5. Project
FROM students        -- 1. Scan
WHERE gpa > 3.0      -- 2. Filter
ORDER BY gpa DESC    -- 3. Sort
LIMIT 2              -- 4. Limit
```

**Physical order (what executes):**
```
1. FROM - Identify data source
2. WHERE - Filter rows
3. SELECT - Project columns
4. ORDER BY - Sort results
5. LIMIT - Truncate
```

Our implementation follows this physical order!

---

## Limitations & Extensions

### Current Limitations

1. **No JOIN support** - Only single table queries
2. **No aggregate functions** - No COUNT, SUM, AVG, etc.
3. **No GROUP BY** - Can't group results
4. **No subqueries** - No nested SELECT
5. **No type checking** - All values treated as double for comparisons
6. **No NULL handling** - Missing values return 0.0

### How Real Databases Extend This

**1. Join Operations:**
```sql
SELECT s.name, c.course_name
FROM students s
JOIN enrollments e ON s.id = e.student_id
JOIN courses c ON e.course_id = c.id
WHERE s.gpa > 3.5
```

**2. Aggregate Functions:**
```sql
SELECT major, COUNT(*), AVG(gpa)
FROM students
GROUP BY major
HAVING AVG(gpa) > 3.0
```

**3. Subqueries:**
```sql
SELECT name FROM students
WHERE gpa > (SELECT AVG(gpa) FROM students)
```

**4. Window Functions:**
```sql
SELECT name, gpa,
       RANK() OVER (ORDER BY gpa DESC) as rank
FROM students
```

---

## Testing Checklist

### Shunting-Yard Algorithm
✅ **Tokenization**
- [x] Numbers (integer and decimal)
- [x] Identifiers (variable names)
- [x] Operators (single and double-char)
- [x] Parentheses

✅ **RPN Conversion**
- [x] Operator precedence (*, / before +, -)
- [x] Right-associativity (^)
- [x] Parentheses handling
- [x] Multiple operators

✅ **Evaluation**
- [x] Arithmetic operators (+, -, *, /, ^)
- [x] Comparison operators (<, >, <=, >=, =, !=)
- [x] Logical operators (&&, ||)
- [x] Variable substitution

### SQL Parser
✅ **Parsing**
- [x] SELECT * (all columns)
- [x] SELECT col1, col2 (specific columns)
- [x] WHERE clause
- [x] ORDER BY ASC/DESC
- [x] LIMIT

✅ **Execution**
- [x] Full table scan (no WHERE)
- [x] WHERE filtering with expressions
- [x] Column projection
- [x] Sorting (ascending and descending)
- [x] Result limiting

✅ **Edge Cases**
- [x] Empty result sets
- [x] Complex WHERE with && and ||
- [x] Multiple ORDER BY columns (not implemented - extension)

---

## Key Takeaways

1. **Shunting-Yard converts infix → RPN in O(n)** with a single stack
2. **RPN evaluation is simple** - just push operands, pop and compute for operators
3. **Operator precedence is critical** for correct evaluation
4. **Query execution is a pipeline** - filter → project → sort → limit
5. **Expression trees are precompiled** in real databases (not evaluated per row)
6. **SQL is declarative** - logical order ≠ physical execution order

### Real-World Impact

```
Query: SELECT name FROM students WHERE age > 20
Data: 1 million rows

Without index:
- Full scan: 1M row comparisons
- Time: ~100ms

With B-Tree index on age:
- Index seek: log₂(1M) ≈ 20 comparisons
- Time: ~0.2ms

Speedup: 500x faster!
```

This is why Lab 4 (B-Trees) and Lab 5 (query processing) work together!

---

## References

- Dijkstra's Shunting-Yard Algorithm (1961)
- PostgreSQL Query Processing: `src/backend/executor/`
- SQLite VDBE (Virtual Database Engine)
- Lab Session Requirements: `../lab_sessions/lab_5.txt`

---

## Author

**Pulasari Jai** (Roll No: 24BCS10656)  
Advanced Database Management Systems  
Scaler Academy
