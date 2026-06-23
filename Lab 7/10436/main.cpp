#include "ShuntingYard.h"
#include "SQLParser.h"
#include <iostream>
#include <vector>

int main() {
    // ---- Part A: Shunting-Yard demo ----
    std::cout << "=== Shunting-Yard Expression Evaluator ===\n";
    ShuntingYard sy;
    std::cout << "3 + 4 * 2     = " << sy.evaluate("3 + 4 * 2")     << "\n";  // 11
    std::cout << "10 / (2 + 3)  = " << sy.evaluate("10 / (2 + 3)")  << "\n";  // 2
    std::cout << "2 ^ 10        = " << sy.evaluate("2 ^ 10")         << "\n";  // 1024
    std::cout << "-(3 + 4)      = " << sy.evaluate("-(3 + 4)")       << "\n";  // -7

    // ---- Part B: SQL parser demo ----
    std::cout << "\n=== SQL Parser Demo ===\n";

    std::vector<Row> students = {
        {{"id","1"},  {"name","Alice"},   {"age","19"}, {"gpa","3.8"}},
        {{"id","2"},  {"name","Bob"},     {"age","22"}, {"gpa","2.9"}},
        {{"id","3"},  {"name","Charlie"}, {"age","20"}, {"gpa","3.5"}},
        {{"id","4"},  {"name","Diana"},   {"age","18"}, {"gpa","4.0"}},
        {{"id","5"},  {"name","Eve"},     {"age","24"}, {"gpa","3.2"}},
        {{"id","6"},  {"name","Frank"},   {"age","21"}, {"gpa","3.7"}},
        {{"id","7"},  {"name","Grace"},   {"age","23"}, {"gpa","2.8"}},
        {{"id","8"},  {"name","Henry"},   {"age","19"}, {"gpa","3.9"}},
        {{"id","9"},  {"name","Ivy"},     {"age","22"}, {"gpa","3.1"}},
        {{"id","10"}, {"name","Jack"},    {"age","20"}, {"gpa","3.6"}},
    };

    SQLParser parser;

    auto q1 = parser.parse("SELECT * FROM students WHERE age > 20");
    print_rows(parser.execute(q1, students), "SELECT * WHERE age > 20");

    auto q2 = parser.parse("SELECT name, gpa FROM students WHERE age >= 18 AND gpa > 3.5");
    print_rows(parser.execute(q2, students), "SELECT name, gpa WHERE age >= 18 AND gpa > 3.5");

    auto q3 = parser.parse("SELECT * FROM students WHERE id = 3");
    print_rows(parser.execute(q3, students), "SELECT * WHERE id = 3");

    auto q4 = parser.parse("SELECT name FROM students WHERE gpa = 4.0");
    print_rows(parser.execute(q4, students), "SELECT name WHERE gpa = 4.0");

    return 0;
}
