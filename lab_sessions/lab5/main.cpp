#include "shunting_yard.h"
#include "sql_parser.h"
#include <iostream>

int main() {
    // Run evaluation engine demo sandbox
    shunting_demo();

    // Context database initialization
    std::vector<Row> students = {
        {{{ "id", 1.0 }, { "name", std::string("Alice") }, { "age", 22.0 }, { "gpa", 3.8 }}},
        {{{ "id", 2.0 }, { "name", std::string("Bob")   }, { "age", 25.0 }, { "gpa", 2.9 }}},
        {{{ "id", 3.0 }, { "name", std::string("Carol") }, { "age", 21.0 }, { "gpa", 3.5 }}},
        {{{ "id", 4.0 }, { "name", std::string("Dave")  }, { "age", 30.0 }, { "gpa", 3.1 }}},
    };

    struct QueryTest { std::string sql; } queries[] = {
        { "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3" },
        { "SELECT * FROM students WHERE age >= 22 && age <= 26" },
    };

    std::cout << "--- SQL Engine Execution Tests ---\n";
    for (auto& [sql] : queries) {
        std::cout << "SQL: " << sql << "\n";
        SelectQuery q = parse_select(sql);
        std::vector<Row> res = execute(q, students);
        print_rows(res);
        std::cout << "\n";
    }
    return 0;
}