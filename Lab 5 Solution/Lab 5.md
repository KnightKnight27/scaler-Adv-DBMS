# Lab Session 5: Dijkstra's Shunting-Yard expression evaluator + minimal SQL SELECT parser over vector<Row>


## The Goal

In this lab, you will build an in-memory SQL compilation and filter system using **two essential milestones**:

1. **Dijkstra's Shunting-Yard Algorithm:** A classic mechanism that converts human-readable math/boolean strings (like `age > 21 AND gpa > 3.5`) into an execution-friendly machine stack format.
2. **A Minimal SQL Query Parser & Plan Executor:** A processing loop that parses a `SELECT` string, isolates target columns, handles conditional filters, and sorts dynamic structured data lists (`vector<Row>`).



# Part 1: The Shunting-Yard Engine

### Why do we need it?

Computers are terrible at reading expressions in standard **Infix notation** (e.g., $A + B \times C$). To know what to run first, a program would constantly have to scan back and forth checking parentheses and algebraic weights.

Dijkstra’s algorithm elegantly solves this by rearranging tokens into **Postfix notation** or **Reverse Polish Notation (RPN)** (e.g., $A$ $B$ $C$ $\times$ $+$). In Postfix, operations appear precisely when they are ready to be executed, allowing a simple linear hardware or software stack to evaluate them in a single fast pass.

### Complete, Unified C++ Code Setup

Create a file named `sql_engine.cpp` and insert the unified codebase below:

If you only want the code **separated properly into files** and **don't want any logic changed**, organize it like this:

### Project Structure

```text
Lab 5 Solution/
│
├── SQLEngine.hpp
├── SQLEngine.cpp
└── main.cpp
```

### SQLEngine.hpp

Only declarations and structures:

```cpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>

struct OpInfo {
    int precedence;
    bool right_assoc;
};

using Value = std::variant<double, std::string>;

struct Row {
    std::unordered_map<std::string, Value> cols;
};

struct SelectQuery {
    std::vector<std::string> columns;
    std::string from;
    std::string where_raw;
    std::string order_by;
    bool order_asc = true;
    int limit = -1;
};

// Expression Processing
std::vector<std::string> tokenize(const std::string& expr);
std::vector<std::string> to_rpn(const std::vector<std::string>& tokens);

double eval_rpn(
    const std::vector<std::string>& rpn,
    const std::unordered_map<std::string, double>& vars
);

void shunting_demo();

// SQL Processing
double row_val(const Row& row, const std::string& col);

std::string to_upper(std::string s);

SelectQuery parse_select(const std::string& sql);

std::vector<Row> execute(
    const SelectQuery& q,
    const std::vector<Row>& data
);

void print_rows(const std::vector<Row>& rows);
```

### SQLEngine.cpp

Put all implementations here:

```cpp
#include "SQLEngine.hpp"

#include <iostream>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <cctype>
#include <cmath>
#include <algorithm>

const std::unordered_map<std::string, OpInfo> OPS = {
    {"||", {1, false}},
    {"&&", {2, false}},
    {"=",  {3, false}},
    {"!=", {3, false}},
    {"<",  {4, false}},
    {">",  {4, false}},
    {"<=", {4, false}},
    {">=", {4, false}},
    {"+",  {5, false}},
    {"-",  {5, false}},
    {"*",  {6, false}},
    {"/",  {6, false}},
    {"^",  {7, true}}
};
```

Then paste:

```cpp
tokenize()
to_rpn()
eval_rpn()
shunting_demo()
row_val()
to_upper()
parse_select()
execute()
print_rows()
```

### main.cpp

```cpp
#include "SQLEngine.hpp"

#include <iostream>
#include <vector>

int main() {
    shunting_demo();

    std::cout << "=== DEMO 2: End-to-End Query Execution ===\n";

    std::vector<Row> students = {
        {{{ "id", 1 }, { "name", std::string("Alice") }, { "age", 22 }, { "gpa", 3.8 }}},
        {{{ "id", 2 }, { "name", std::string("Bob") }, { "age", 25 }, { "gpa", 2.9 }}},
        {{{ "id", 3 }, { "name", std::string("Carol") }, { "age", 21 }, { "gpa", 3.5 }}},
        {{{ "id", 4 }, { "name", std::string("Dave") }, { "age", 30 }, { "gpa", 3.1 }}},
    };

    struct QueryTestCase {
        std::string sql;
    };

    QueryTestCase queries[] = {
        { "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3" },
        { "SELECT * FROM students WHERE age >= 22 && age <= 26" }
    };

    for (auto& [sql] : queries) {
        std::cout << "SQL input : " << sql << "\n";

        auto query = parse_select(sql);
        auto result = execute(query, students);

        std::cout << "Results:\n";
        print_rows(result);

        std::cout << "\n";
    }

    return 0;
}
```

Compile:

```bash
g++ main.cpp SQLEngine.cpp -std=c++17 -o sql_parser
```

## How the Architectural Pipeline Connects

Every standard database processing engine processes incoming string statements through these four main steps:

1. **The Lexer (`tokenize`):** Converts a single uninterrupted string line into separated atomic word and operator segments.
2. **The Parser (`parse_select`):** Converts the plain array of tokens into a structured `SelectQuery` layout object (known as an Abstract Syntax Tree or AST).
3. **The Engine Optimizer:** Converts structural conditions into Postfix Notation via **Shunting-Yard** to optimize performance before starting data retrieval loops.
4. **The Executor (`execute`):** Loops over your table columns sequentially, matching rows using postfix validation loops, and outputs a refined `vector<Row>` array result.

---

## Quick Cheat Sheet

| Operation Node | Architectural Duty | Underlying C++ Tool Used |
| --- | --- | --- |
| **Filter Component (WHERE)** | Limits the quantity of rows evaluated | `eval_rpn()` via stack loops |
| **Projection Mapping (SELECT)** | Limits the width of visible columns | `std::unordered_map::count()` key transfers |
| **Sort Node (ORDER BY)** | Arranges rows based on a target property | Custom predicate lambda `std::sort()` |
| **Truncation Block (LIMIT)** | Cuts off the array size at a maximum capacity | `std::vector::resize()` truncation |