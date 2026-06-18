#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>

using Cell = std::variant<double, std::string>;

struct Record
{
    std::unordered_map<std::string, Cell> fields;
};

struct QueryInfo
{
    std::vector<std::string> selectedColumns;
    std::string tableName;
    std::string condition;
    std::string sortColumn;
    bool ascending = true;
    int rowLimit = -1;
};

void demonstrateShuntingYard();

QueryInfo buildQuery(const std::string& statement);

std::vector<Record> runQuery(
    const QueryInfo& query,
    const std::vector<Record>& records
);

void displayRecords(
    const std::vector<Record>& records
);

int main()
{
    demonstrateShuntingYard();

    std::vector<Record> database =
    {
        {
            {
                {"id", 1.0},
                {"name", std::string("Alice")},
                {"age", 22.0},
                {"gpa", 3.8}
            }
        },

        {
            {
                {"id", 2.0},
                {"name", std::string("Bob")},
                {"age", 25.0},
                {"gpa", 2.9}
            }
        },

        {
            {
                {"id", 3.0},
                {"name", std::string("Carol")},
                {"age", 21.0},
                {"gpa", 3.5}
            }
        },

        {
            {
                {"id", 4.0},
                {"name", std::string("Dave")},
                {"age", 30.0},
                {"gpa", 3.1}
            }
        }
    };

    std::vector<std::string> sqlStatements =
    {
        "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3",

        "SELECT * FROM students WHERE age >= 22 && age <= 26"
    };

    for (const auto& statement : sqlStatements)
    {
        std::cout
            << "Executing Query:\n"
            << statement
            << "\n\n";

        QueryInfo query =
            buildQuery(statement);

        std::vector<Record> output =
            runQuery(query, database);

        displayRecords(output);

        std::cout
            << "\n--------------------------------\n\n";
    }

    return 0;
}