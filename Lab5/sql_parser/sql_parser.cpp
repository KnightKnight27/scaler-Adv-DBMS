#include <iostream>
#include <vector>
#include <string>
#include <sstream>

struct Row {
    int id;
    std::string name;
    int age;
};

void executeQuery(
    const std::vector<Row>& table,
    const std::string& query
) {
    std::stringstream ss(query);

    std::string selectKeyword;
    std::string column;
    std::string fromKeyword;
    std::string tableName;

    ss >> selectKeyword
       >> column
       >> fromKeyword
       >> tableName;

    std::string whereKeyword;
    std::string whereColumn;
    std::string op;
    int whereValue;

    bool hasWhere = false;

    if (ss >> whereKeyword) {

        hasWhere = true;

        ss >> whereColumn
           >> op
           >> whereValue;
    }

    for (const auto& row : table) {

        bool matches = true;

        if (hasWhere) {

            if (whereColumn == "age") {

                if (op == ">")
                    matches = row.age > whereValue;

                else if (op == "<")
                    matches = row.age < whereValue;

                else if (op == "=")
                    matches = row.age == whereValue;
            }

            else if (whereColumn == "id") {

                if (op == ">")
                    matches = row.id > whereValue;

                else if (op == "<")
                    matches = row.id < whereValue;

                else if (op == "=")
                    matches = row.id == whereValue;
            }
        }

        if (!matches)
            continue;

        if (column == "*") {

            std::cout
                << row.id << " "
                << row.name << " "
                << row.age
                << std::endl;
        }

        else if (column == "id") {

            std::cout
                << row.id
                << std::endl;
        }

        else if (column == "name") {

            std::cout
                << row.name
                << std::endl;
        }

        else if (column == "age") {

            std::cout
                << row.age
                << std::endl;
        }
    }
}

int main() {

    std::vector<Row> students = {
        {1, "Alice", 20},
        {2, "Bob", 25},
        {3, "Charlie", 18},
        {4, "David", 22}
    };

    std::cout
        << "Query 1:\n";

    executeQuery(
        students,
        "SELECT * FROM students"
    );

    std::cout
        << "\nQuery 2:\n";

    executeQuery(
        students,
        "SELECT name FROM students WHERE age > 20"
    );

    std::cout
        << "\nQuery 3:\n";

    executeQuery(
        students,
        "SELECT id FROM students WHERE age < 21"
    );

    return 0;
}