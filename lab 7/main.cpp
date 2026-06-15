#include "lexer.h"
#include "parser.h"
#include "executor.h"
#include <iostream>
#include <vector>

int main() {
    // In-memory Database Records
    std::vector<Row> students = {
        {{"id", 1}, {"name", std::string("Alice")},   {"age", 22}, {"gpa", 3.8},  {"status", std::string("Active")}},
        {{"id", 2}, {"name", std::string("Bob")},     {"age", 25}, {"gpa", 2.9},  {"status", std::string("Probation")}},
        {{"id", 3}, {"name", std::string("Charlie")}, {"age", 20}, {"gpa", 3.95}, {"status", std::string("Active")}},
        {{"id", 4}, {"name", std::string("Dave")},    {"age", 28}, {"gpa", 3.1},  {"status", std::string("Graduated")}},
        {{"id", 5}, {"name", std::string("Emma")},    {"age", 21}, {"gpa", 3.5},  {"status", std::string("Active")}},
        {{"id", 6}, {"name", std::string("Frank")},   {"age", 19}, {"gpa", 2.7},  {"status", std::string("Probation")}}
    };

    std::vector<std::string> queries = {
        "SELECT name, age, gpa FROM students WHERE gpa > 3.0 AND age <= 22 ORDER BY gpa DESC LIMIT 2",
        "SELECT * FROM students WHERE status = 'Active' OR gpa >= 3.8 ORDER BY age ASC",
        "SELECT id, name, status FROM students WHERE status != 'Active' AND age > 20 ORDER BY id DESC",
        "SELECT * FROM students"
    };

    std::cout << "========================================================\n";
    std::cout << "          ADVANCED SQL PARSER & QUERY ENGINE DEMO       \n";
    std::cout << "========================================================\n\n";

    for (const auto& sql : queries) {
        std::cout << "Executing SQL Query: \"" << sql << "\"\n";
        try {
            Lexer lexer(sql);
            auto tokens = lexer.tokenize();

            DbParser parser(tokens);
            SelectStatement stmt = parser.parseSelect();

            auto result = Executor::execute(stmt, students);
            Executor::printTable(result);
        } catch (const std::exception& e) {
            std::cerr << "Query execution error: " << e.what() << "\n\n";
        }
    }

    return 0;
}
