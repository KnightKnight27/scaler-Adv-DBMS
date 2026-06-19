#include <iostream>
#include <string>
#include <vector>

#include "shunting_yard.cpp"
#include "sql_parser.cpp"

int main() {
  shunting_demo();

  std::vector<Row> students;

  students.push_back(make_row(11, "Aarav", 20, 8.7));
  students.push_back(make_row(12, "Parth", 22, 9.1));
  students.push_back(make_row(13, "Rohan", 19, 7.8));
  students.push_back(make_row(14, "Rohit", 21, 8.9));

  std::string queries[] = {
      "SELECT id, name, gpa FROM students WHERE gpa > 8.5 ORDER BY gpa DESC LIMIT 2",

      "SELECT * FROM students WHERE age >= 20 AND age <= 22",

      "SELECT name, age FROM students WHERE age < 21 OR gpa > 9"};

  for (const auto& sql : queries) {
    std::cout << "SQL: " << sql << "\n\n";

    SelectQuery parsed = parse_select(sql);

    std::vector<Row> result = execute(parsed, students);

    print_rows(result);

    std::cout << "\n";
  }

  return 0;
}