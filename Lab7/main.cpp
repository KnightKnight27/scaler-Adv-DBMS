#include "sql_parser.cpp"
#include <iostream>
#include <string>
#include <vector>

struct QueryTask {
  std::string sqlText;
};

int main() {
  runShuntingYardDemo();

  std::vector<DataRecord> studentRecords;
  studentRecords.push_back(createNewRecord(1, "Alice", 22, 3.8));
  studentRecords.push_back(createNewRecord(2, "Bob", 25, 2.9));
  studentRecords.push_back(createNewRecord(3, "Carol", 21, 3.5));
  studentRecords.push_back(createNewRecord(4, "Dave", 30, 3.1));

  std::vector<QueryTask> queriesToExecute = {
      {"SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC "
       "LIMIT 3"},
      {"SELECT * FROM students WHERE age >= 22 && age <= 26"}};

  for (const auto &task : queriesToExecute) {
    std::cout << "SQL: " << task.sqlText << "\n";
    ParsedQuery selectStmt = parseSelectStatement(task.sqlText);
    std::vector<DataRecord> filteredRecords =
        executeSelectQuery(selectStmt, studentRecords);
    outputResultSet(filteredRecords);
    std::cout << "\n";
  }

  return 0;
}