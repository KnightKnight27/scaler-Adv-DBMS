#include "SQLEngine.hpp"

int main() {

    std::vector<Row> employees = {
        {{"id", 1}, {"age", 24}, {"salary", 80000}},
        {{"id", 2}, {"age", 28}, {"salary", 85000}},
        {{"id", 3}, {"age", 35}, {"salary", 110000}},
        {{"id", 4}, {"age", 22}, {"salary", 75000}},
        {{"id", 5}, {"age", 40}, {"salary", 150000}}
    };

    SQLEngine db;

    try {
        db.ExecuteSelect("SELECT id, salary WHERE salary * 1.1 > 90000", employees);

        db.ExecuteSelect("SELECT id, age, salary WHERE age > 25 AND salary < 120000", employees);

        db.ExecuteSelect("SELECT id, age WHERE age > 25 AND salary < 100000 OR age < 23", employees);

    } catch (const std::exception& e) {
        std::cerr << "Execution Error: " << e.what() << "\n";
    }

    return 0;
}