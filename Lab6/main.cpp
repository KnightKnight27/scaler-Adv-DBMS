#include "Types.h"
#include "Parser.h"
#include <iostream>
#include <vector>

void printTable(const Table& table) {
    if (table.empty()) {
        std::cout << "Empty Result Set\n";
        return;
    }
    // Print column names from the first row
    bool firstRow = true;
    for (const auto& row : table) {
        if (firstRow) {
            for (const auto& [col, val] : row) {
                std::cout << col << "\t| ";
            }
            std::cout << "\n----------------------------------------\n";
            firstRow = false;
        }
        for (const auto& [col, val] : row) {
            printValue(val);
            std::cout << "\t| ";
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

int main() {
    Table table = {
        {{"id", 1}, {"name", std::string("Alice")}, {"age", 25}, {"salary", 60000}},
        {{"id", 2}, {"name", std::string("Bob")}, {"age", 30}, {"salary", 40000}},
        {{"id", 3}, {"name", std::string("Charlie")}, {"age", 20}, {"salary", 30000}},
        {{"id", 4}, {"name", std::string("Diana")}, {"age", 28}, {"salary", 70000}}
    };

    std::cout << "Available Data (Table 'users'):\n";
    printTable(table);

    std::vector<std::string> queries = {
        "SELECT * FROM users;",
        "SELECT name, salary FROM users WHERE age > 25;",
        "SELECT * FROM users WHERE salary > 30000 AND age < 30;",
        "SELECT id FROM users WHERE name = 'Charlie';",
        "SELECT * FROM users WHERE age * 2 >= 60;"
    };

    for (const auto& q : queries) {
        std::cout << "Executing: " << q << "\n";
        try {
            Table result = Parser::executeSelect(q, table);
            printTable(result);
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n\n";
        }
    }

    return 0;
}
