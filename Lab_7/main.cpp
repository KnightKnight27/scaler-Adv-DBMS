#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include "SQL_parser.cpp"

int main()
{
    runShuntingYardDemo();

    std::vector<Record> students;
    students.push_back(createRecord(1, "Alice", 22, 3.8));
    students.push_back(createRecord(2, "Bob", 25, 2.9));
    students.push_back(createRecord(3, "Carol", 21, 3.5));
    students.push_back(createRecord(4, "Dave", 30, 3.1));

    struct QueryDef
    {
        std::string sqlQueryString;
    };
    QueryDef queryList[2];
    queryList[0].sqlQueryString = "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3";
    queryList[1].sqlQueryString = "SELECT * FROM students WHERE age >= 22 && age <= 26";

    for (int idx = 0; idx < 2; ++idx)
    {
        std::cout << "SQL: " << queryList[idx].sqlQueryString << "\n";
        ParsedSelect selectPlan = parseSelectSQL(queryList[idx].sqlQueryString);
        std::vector<Record> outputDataset = runQueryPlan(selectPlan, students);
        displayRecords(outputDataset);
        std::cout << "\n";
    }

    return 0;
}