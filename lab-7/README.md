# Lab 7: Shunting-Yard Algorithm + SQL Parser

## Overview
Implementation of SQL query processing with:
- **Dijkstra's Shunting-Yard Algorithm** for expression evaluation
- **Recursive Descent Parser** for SQL SELECT statements  
- **Query Executor** for filtering, projection, sorting, and limiting

---

## Folder Structure
```
lab-7/
└── app/
    ├── types.h           # Value, Row, Token types
    ├── expressions.h     # Expression AST nodes
    ├── lexer.h/cpp       # SQL tokenizer
    ├── parser.h          # SQL parser (header-only)
    ├── executor.h        # Query executor + Shunting-Yard (header-only)
    ├── main.cpp          # Demos
    └── Makefile          # Build config
```

## Compilation

```bash
cd app
g++ -std=c++17 -O2 -o sql_parser main.cpp lexer.cpp
./sql_parser
```

## Components

### 1. Lexer (`lexer.h`/`lexer.cpp`)
Converts SQL string → tokens

**Tokens**: SELECT, FROM, WHERE, ORDER, BY, DESC, ASC, LIMIT, identifiers, numbers, operators

### 2. SelectParser (`parser.h`)
Converts tokens → `SelectQuery` AST using recursive descent

**Grammar**:
```
SELECT ( column_list | * ) FROM table [ WHERE expr ] [ ORDER BY col [ASC|DESC] ] [ LIMIT n ]
```

### 3. ExpressionEvaluator (in `executor.h`)
Shunting-Yard algorithm for expression evaluation

**Steps**:
1. Tokenize infix expression
2. Convert to postfix (RPN) using operator stack
3. Evaluate postfix with value stack

**Operator Precedence**:
- Level 1: `||` (OR)
- Level 2: `&&` (AND)
- Level 3: `=`, `!=` (equality)
- Level 4: `<`, `>`, `<=`, `>=` (comparison)
- Level 5: `+`, `-` (addition)
- Level 6: `*`, `/` (multiplication)
- Level 7: `^` (exponentiation, right-associative)

### 4. QueryExecutor (in `executor.h`)
Executes parsed queries against in-memory data

**Pipeline**:
1. **WHERE**: Filter rows using Shunting-Yard expression evaluation
2. **PROJECT**: Select specified columns
3. **ORDER BY**: Sort results
4. **LIMIT**: Truncate result set

## Data Types

```cpp
using Value = std::variant<double, std::string>;

struct Row {
    std::unordered_map<std::string, Value> cols;
};

struct SelectQuery {
    std::vector<std::string> columns;  // empty = SELECT *
    std::string table;
    std::string where_clause;          // raw expression
    std::string order_by;
    bool order_asc = true;
    int limit = -1;
};
```

## Example Usage

```cpp
// 1. Tokenize SQL
Lexer lexer("SELECT name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC");
auto tokens = lexer.tokenize();

// 2. Parse to AST
SelectParser parser(tokens);
auto query = parser.parse();

// 3. Execute against data
QueryExecutor executor;
auto results = executor.execute(query, students_data);

// 4. Print results
executor.print_rows(results);
```

## Demos

### Demo 1: Shunting-Yard
Tests infix → postfix conversion:
- `age * 2 + salary / 1000 > 100`
- `gpa > 3.0 && age < 25`
- `2 + 3 * 4`

### Demo 2: SQL Queries
Full SELECT query execution with:
- Column projection
- WHERE filtering with boolean/arithmetic expressions
- ORDER BY with ASC/DESC
- LIMIT row count

### Demo 3: Complex WHERE
Arithmetic expressions in predicates:
- `salary > 50000 && exp >= 8`
- `salary / 1000 > 50`

## Key Features

✅ **Correct operator precedence** - `2 + 3 * 4 = 14` (not 20)
✅ **Associativity support** - Right-associative operators like `^`
✅ **Mixed data types** - Numbers and strings in columns
✅ **Type-safe evaluation** - `std::variant` prevents type errors
✅ **Error handling** - Descriptive exceptions for syntax errors
✅ **Efficient parsing** - Single pass recursive descent
✅ **Linear evaluation** - O(n) tokenization and RPN conversion

## Algorithm Complexity

- **Tokenization**: O(n)
- **Shunting-Yard**: O(n) with O(k) stack space (k = operator count)
- **RPN Evaluation**: O(n)
- **Query Execution**: O(n) filtering + O(n log n) sorting + O(n) projection
- **Overall**: O(n log n) dominated by sorting

## Design Patterns

1. **Single Responsibility** - Each class has one clear purpose
2. **Separation of Concerns** - Lexing, parsing, evaluation are independent
3. **Header-Only Templates** - Parser and executor use inline implementations
4. **Visitor Pattern** - Expression evaluation traverses AST
5. **Pipeline** - Data flows through well-defined stages

## Connection to Real Databases

This implementation demonstrates the fundamental architecture used in:
- **PostgreSQL**: Lexer → Parser → Planner → Executor
- **MySQL**: Similar pipeline with additional optimization passes
- **SQLite**: Simplified version for embedded use

The WHERE clause evaluation using Shunting-Yard is found in:
- **Expression evaluators** in all SQL databases
- **Spreadsheet applications** (Excel, LibreOffice)
- **Calculators** with operator precedence

## Extensions

1. **Aggregate Functions**: `COUNT()`, `SUM()`, `AVG()`, `MAX()`, `MIN()`
2. **GROUP BY**: Partition and aggregate results
3. **String Functions**: `UPPER()`, `LOWER()`, `LENGTH()`, `SUBSTR()`
4. **Pattern Matching**: `LIKE` operator with `%` and `_` wildcards
5. **JOINs**: `INNER JOIN`, `LEFT JOIN` on multiple tables
6. **Indexing**: Skip full table scan for indexed columns
7. **Query Optimization**: Choose best execution plan based on statistics



