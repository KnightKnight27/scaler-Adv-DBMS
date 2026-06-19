#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include "sql_parser.cpp"

int main()
{
    shunting_demo();

    std::vector<Row> students;
    students.push_back(make_row(1, "Alice", 22, 3.8));
    students.push_back(make_row(2, "Bob", 25, 2.9));
    students.push_back(make_row(3, "Carol", 21, 3.5));
    students.push_back(make_row(4, "Dave", 30, 3.1));

    struct Query
    {
        std::string sql;
    };
    Query queries[2];
    queries[0].sql = "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3";
    queries[1].sql = "SELECT * FROM students WHERE age >= 22 && age <= 26";

    for (int qi = 0; qi < 2; ++qi)
    {
        std::cout << "SQL: " << queries[qi].sql << "\n";
        SelectQuery parsed = parse_select(queries[qi].sql);
        std::vector<Row> res = execute(parsed, students);
        print_rows(res);
        std::cout << "\n";
    }

    return 0;
}