#include "SQLEngine.hpp"
#include <vector>
#include <iostream>

int main() {
    
    // Updated Mock Database Storage Records
    
    std::vector<Row> employees = {
        {{"id", 1}, {"age", 26}, {"salary", 95000}},
        {{"id", 2}, {"age", 31}, {"salary", 120000}},
        {{"id", 3}, {"age", 19}, {"salary", 45000}},
        {{"id", 4}, {"age", 45}, {"salary", 185000}},
        {{"id", 5}, {"age", 29}, {"salary", 88000}}
    };

    SQLEngine db;

    
    // Query Execution Pipeline Verification
    
    try {
        // Test Case 1: Mathematical arithmetic (Simulating a 5% bonus check)
        db.ExecuteSelect("SELECT id, salary WHERE salary * 1.05 > 100000", employees);

        // Test Case 2: Multi-conditional intersection (Mid-level income brackets)
        db.ExecuteSelect("SELECT id, age, salary WHERE age > 25 AND salary < 150000", employees);

        // Test Case 3: Complex evaluation (Targeting junior staff OR high earners)
        db.ExecuteSelect("SELECT id, age WHERE age > 30 AND salary < 200000 OR age < 21", employees);

    } catch (const std::exception& e) {
        std::cerr << "CRITICAL Execution Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}