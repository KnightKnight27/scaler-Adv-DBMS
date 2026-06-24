/**
 * Lab 5 — Shunting-Yard + SQL Parser: Driver Program
 */

#include "shunting_yard.h"
#include "sql_parser.h"
#include <iostream>
#include <cassert>

// ─────────────────────────────────────────────────
// Part 1: Shunting-Yard Expression Evaluator Tests
// ─────────────────────────────────────────────────
void test_shunting_yard() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Part 1: Dijkstra's Shunting-Yard Expression Evaluator     ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    // Test arithmetic expressions
    std::cout << "\n--- Arithmetic Expressions ---" << std::endl;
    struct TestCase { std::string expr; double expected; };
    std::vector<TestCase> arith_tests = {
        {"3 + 4",                7},
        {"10 - 3 * 2",           4},
        {"(10 - 3) * 2",        14},
        {"2 + 3 * 4 - 1",       13},
        {"(2 + 3) * (4 - 1)",   15},
        {"100 / 4 / 5",          5},
        {"2 * 3 + 4 * 5",       26},
        {"10 % 3",               1},
        {"((2 + 3) * 4)",       20},
    };

    for (const auto& test : arith_tests) {
        Value result = ShuntingYard::eval(test.expr);
        double val = value_to_double(result);
        bool pass = std::abs(val - test.expected) < 0.001;
        std::cout << "  " << std::setw(25) << test.expr
                  << " = " << std::setw(6) << val
                  << (pass ? " ✓" : " ✗ FAIL") << std::endl;
    }

    // Show infix → postfix conversion
    std::cout << "\n--- Infix → Postfix Conversion ---" << std::endl;
    std::vector<std::string> exprs = {
        "3 + 4 * 2",
        "(3 + 4) * 2",
        "a > 5 AND b < 10",
        "x == 1 OR (y > 2 AND z < 3)",
    };
    for (const auto& expr : exprs) {
        auto tokens = ExprTokenizer::tokenize(expr);
        auto postfix = ShuntingYard::to_postfix(tokens);
        std::cout << "  Infix:   " << expr << std::endl;
        std::cout << "  Postfix: ";
        for (const auto& t : postfix) std::cout << t.to_string() << " ";
        std::cout << std::endl << std::endl;
    }

    // Test comparison and logical operators
    std::cout << "--- Comparison & Logical Operators ---" << std::endl;
    std::unordered_map<std::string, Value> vars = {
        {"age", 25.0}, {"salary", 50000.0}, {"name", std::string("Alice")},
        {"active", 1.0}, {"score", 85.0}
    };

    std::vector<std::pair<std::string, bool>> logic_tests = {
        {"age > 20",                    true},
        {"age < 20",                    false},
        {"age >= 25",                   true},
        {"salary == 50000",             true},
        {"name == 'Alice'",             true},
        {"name != 'Bob'",              true},
        {"age > 20 AND salary > 40000", true},
        {"age > 30 OR salary > 40000",  true},
        {"age > 30 AND salary > 60000", false},
        {"NOT age > 30",                true},
        {"score >= 80 AND active == 1", true},
    };

    for (const auto& [expr, expected] : logic_tests) {
        Value result = ShuntingYard::eval(expr, vars);
        bool val = value_to_bool(result);
        bool pass = (val == expected);
        std::cout << "  " << std::setw(35) << expr
                  << " → " << std::setw(5) << (val ? "true" : "false")
                  << (pass ? " ✓" : " ✗ FAIL") << std::endl;
    }
}

// ─────────────────────────────────────────────────
// Part 2: SQL SELECT Parser Tests
// ─────────────────────────────────────────────────
void test_sql_parser() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Part 2: SQL SELECT Parser over vector<Row>                ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    // Create a sample table
    Table employees;
    employees.name = "employees";
    employees.columns = {"id", "name", "department", "salary", "age"};

    employees.add_row({1.0, std::string("Alice"),   std::string("Engineering"), 95000.0, 30.0});
    employees.add_row({2.0, std::string("Bob"),     std::string("Marketing"),   65000.0, 28.0});
    employees.add_row({3.0, std::string("Charlie"), std::string("Engineering"), 110000.0, 35.0});
    employees.add_row({4.0, std::string("Diana"),   std::string("Sales"),       72000.0, 32.0});
    employees.add_row({5.0, std::string("Eve"),     std::string("Engineering"), 88000.0, 27.0});
    employees.add_row({6.0, std::string("Frank"),   std::string("Marketing"),   71000.0, 31.0});
    employees.add_row({7.0, std::string("Grace"),   std::string("Sales"),       68000.0, 29.0});
    employees.add_row({8.0, std::string("Hank"),    std::string("Engineering"), 120000.0, 40.0});

    SQLExecutor executor;
    executor.register_table(employees);

    // Query 1: SELECT all
    std::cout << "\n--- Query 1: SELECT * ---" << std::endl;
    executor.print_results("SELECT * FROM employees");

    // Query 2: SELECT specific columns
    std::cout << "\n--- Query 2: SELECT specific columns ---" << std::endl;
    executor.print_results("SELECT name, salary FROM employees");

    // Query 3: WHERE clause
    std::cout << "\n--- Query 3: WHERE filter ---" << std::endl;
    executor.print_results("SELECT name, salary, department FROM employees WHERE salary > 80000");

    // Query 4: Complex WHERE with AND
    std::cout << "\n--- Query 4: WHERE with AND ---" << std::endl;
    executor.print_results(
        "SELECT name, salary, age FROM employees WHERE salary > 70000 AND age < 35"
    );

    // Query 5: WHERE with string comparison
    std::cout << "\n--- Query 5: WHERE with string comparison ---" << std::endl;
    executor.print_results(
        "SELECT name, salary FROM employees WHERE department == 'Engineering'"
    );

    // Query 6: ORDER BY
    std::cout << "\n--- Query 6: ORDER BY ---" << std::endl;
    executor.print_results("SELECT name, salary FROM employees ORDER BY salary DESC");

    // Query 7: LIMIT
    std::cout << "\n--- Query 7: ORDER BY + LIMIT ---" << std::endl;
    executor.print_results("SELECT name, salary FROM employees ORDER BY salary DESC LIMIT 3");

    // Query 8: Complex query
    std::cout << "\n--- Query 8: Complex query ---" << std::endl;
    executor.print_results(
        "SELECT name, salary, age FROM employees WHERE salary >= 70000 AND age <= 35 ORDER BY age LIMIT 5"
    );

    // Show parsed structure
    std::cout << "\n--- Parse Tree Demo ---" << std::endl;
    SQLParser parser;
    auto stmt = parser.parse(
        "SELECT name, salary FROM employees WHERE salary > 80000 AND age < 35 ORDER BY salary DESC LIMIT 5"
    );
    std::cout << "  Columns: ";
    for (const auto& c : stmt.columns) std::cout << c << " ";
    std::cout << std::endl;
    std::cout << "  Table:   " << stmt.table_name << std::endl;
    std::cout << "  WHERE:   " << stmt.where_clause << std::endl;
    std::cout << "  ORDER BY:" << stmt.order_by << " " << (stmt.order_desc ? "DESC" : "ASC") << std::endl;
    std::cout << "  LIMIT:   " << stmt.limit << std::endl;
}

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Lab 5: Expression Evaluator + SQL SELECT Parser           ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    test_shunting_yard();
    test_sql_parser();

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Lab 5 Complete!                                            ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    return 0;
}
