# Minimal SQL Query Parser

This directory contains a C++ implementation of a minimal SQL query parser. It is designed to extract selected columns, table names, and parse the `WHERE` clause of a SQL query into a postfix representation using Dijkstra's Shunting Yard algorithm.

## Prerequisites

- A C++17 compatible compiler (e.g., `g++` or `clang++`).

## How to Compile

Open your terminal, navigate to this directory (`lab7_parser`), and run the following command to compile the code:

```bash
g++ -std=c++17 main.cpp -o parser
```

This will generate an executable file named `parser`.

## How to Run

Once compiled, you can run the executable with the following command:

```bash
./parser
```

The program will output the parsing results for the built-in test queries to the terminal.

## Adding Custom Queries

If you want to test the parser with your own SQL queries, follow these steps:

1. Open `main.cpp` in your text editor.
2. Scroll to the bottom of the file to the `main()` function.
3. Locate the `testQueries` vector.
4. Add your custom SQL string to the list. For example:
   ```cpp
   vector<string> testQueries = {
       // ... existing queries ...
       "SELECT * FROM my_table WHERE x = 10 AND y < 20;"
   };
   ```
5. Save the file, recompile the code, and run it again using the instructions above.
