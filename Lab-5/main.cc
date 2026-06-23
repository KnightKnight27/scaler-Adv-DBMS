// Lab 5 - demo driver.
//
// Part 1: shunting-yard expression evaluation.
// Part 2: a few SELECT queries run over an in-memory table of students.

#include <iostream>
#include <string>
#include <vector>

#include "shunting_yard.h"
#include "sql.h"

namespace {

void shunting_yard_demo() {
    std::cout << "=== Part 1: Shunting-Yard ===\n";

    struct Case {
        std::string expr;
        std::unordered_map<std::string, double> vars;
    };

    std::vector<Case> cases = {
        {"3 + 4 * 2", {}},                                   // 11
        {"( 3 + 4 ) * 2", {}},                               // 14
        {"age * 2 + 10 > 60", {{"age", 30}}},                // true
        {"age >= 18 && age <= 25", {{"age", 22}}},           // true
        {"salary / 1000 < 50 || age > 60", {{"salary", 90000}, {"age", 40}}},  // false
    };

    for (const auto& c : cases) {
        auto tokens = lab5::tokenize(c.expr);
        auto rpn    = lab5::to_rpn(tokens);

        std::cout << "expr : " << c.expr << "\n";
        std::cout << "rpn  : ";
        for (const auto& t : rpn) std::cout << t << " ";
        std::cout << "\n";

        double r = lab5::eval_rpn(rpn, c.vars);
        // If the top operator was a comparison/logical, show true/false;
        // otherwise show the numeric value.
        std::cout << "value: " << r << "\n\n";
    }
}

void sql_demo() {
    std::cout << "=== Part 2: Minimal SQL SELECT ===\n";

    using lab5::Row;
    std::vector<Row> students = {
        {{{"id", 1.0}, {"name", std::string("Alice")}, {"age", 22.0}, {"gpa", 3.8}}},
        {{{"id", 2.0}, {"name", std::string("Bob")},   {"age", 25.0}, {"gpa", 2.9}}},
        {{{"id", 3.0}, {"name", std::string("Carol")}, {"age", 21.0}, {"gpa", 3.5}}},
        {{{"id", 4.0}, {"name", std::string("Dave")},  {"age", 30.0}, {"gpa", 3.1}}},
        {{{"id", 5.0}, {"name", std::string("Eve")},   {"age", 19.0}, {"gpa", 3.9}}},
    };

    std::vector<std::string> queries = {
        "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3",
        "SELECT * FROM students WHERE age >= 22 && age <= 26",
        "SELECT name, age FROM students ORDER BY age ASC",
        "SELECT * FROM students WHERE gpa > 5.0",   // no matches
    };

    for (const std::string& sql : queries) {
        std::cout << "SQL: " << sql << "\n";
        auto q   = lab5::parse_select(sql);
        auto res = lab5::execute(q, students);
        lab5::print_rows(res);
        std::cout << "\n";
    }
}

}  // namespace

int main() {
    shunting_yard_demo();
    sql_demo();
    std::cout << "All Lab 5 demos ran.\n";
    return 0;
}
