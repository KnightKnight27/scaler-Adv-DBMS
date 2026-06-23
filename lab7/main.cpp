#include "db_engine.h"
#include <iostream>
#include <string>
#include <vector>

void seedDatabase(Database& db) {
    // 1. Create and Seed Students Table
    db.createTable("students", {"student_id", "first_name", "last_name", "age", "gpa", "course"});
    db.insertInto("students", {"1", "Alice", "Smith", "20", "3.85", "CS"});
    db.insertInto("students", {"2", "Bob", "Jones", "22", "3.40", "EE"});
    db.insertInto("students", {"3", "Charlie", "Brown", "19", "3.92", "CS"});
    db.insertInto("students", {"4", "David", "Wilson", "21", "2.95", "ME"});
    db.insertInto("students", {"5", "Eva", "Davis", "20", "3.70", "EE"});
    db.insertInto("students", {"6", "Frank", "Miller", "23", "3.15", "CS"});

    // 2. Create and Seed Courses Table
    db.createTable("courses", {"course_id", "course_name", "credits", "department"});
    db.insertInto("courses", {"CS101", "Introduction to CS", "4", "CS"});
    db.insertInto("courses", {"EE201", "Circuit Analysis", "3", "EE"});
    db.insertInto("courses", {"MATH301", "Linear Algebra", "4", "MATH"});
    db.insertInto("courses", {"ME102", "Engineering Mechanics", "3", "ME"});
}

void runAutomatedTests(const Database& db) {
    std::cout << "==========================================================" << std::endl;
    std::cout << "           RUNNING AUTOMATED REPRESETATIVE TESTS           " << std::endl;
    std::cout << "==========================================================" << std::endl;

    // Test 1: Simple projection of all columns (SELECT *)
    db.executeQuery("SELECT * FROM students;");

    // Test 2: Filter query with numeric inequality
    db.executeQuery("SELECT student_id, first_name, gpa FROM students WHERE gpa > 3.50;");

    // Test 3: Compound filter with logical AND
    db.executeQuery("SELECT first_name, last_name, age, course FROM students WHERE age >= 21 AND course = 'CS';");

    // Test 4: Nested logic with parentheses and OR
    db.executeQuery("SELECT first_name, course, gpa FROM students WHERE (course = 'CS' OR course = 'EE') AND gpa >= 3.70;");

    // Test 5: Unary operator (NOT)
    db.executeQuery("SELECT first_name, course FROM students WHERE NOT course = 'CS';");

    // Test 6: Arithmetic expression check inside filter
    db.executeQuery("SELECT first_name, age FROM students WHERE age * 2 > 40;");

    // Test 7: Error - Non-existent column projection
    db.executeQuery("SELECT first_name, unknown_col FROM students;");

    // Test 8: Error - Syntax invalid SELECT statement
    db.executeQuery("SELECT FROM students;");

    // Test 9: Error - Non-existent table name
    db.executeQuery("SELECT * FROM faculty;");

    std::cout << "\n==========================================================" << std::endl;
    std::cout << "            AUTOMATED TESTS COMPLETE. ENTER REPL          " << std::endl;
    std::cout << "==========================================================" << std::endl;
}

int main(int argc, char* argv[]) {
    Database db;
    seedDatabase(db);

    // If an argument "test" is passed, run tests and exit
    if (argc > 1 && std::string(argv[1]) == "test") {
        runAutomatedTests(db);
        return 0;
    }

    runAutomatedTests(db);

    std::cout << "\nInteractive SQL SELECT Parser Shell. Type 'exit' to quit." << std::endl;
    std::cout << "Tables available: 'students', 'courses'" << std::endl;
    
    std::string line;
    while (true) {
        std::cout << "SQL> ";
        if (!std::getline(std::cin, line)) {
            break;
        }
        
        // Trim spaces
        while (!line.empty() && std::isspace(line.back())) line.pop_back();
        while (!line.empty() && std::isspace(line.front())) line.erase(line.begin());

        if (line.empty()) continue;
        if (line == "exit" || line == "quit") {
            break;
        }

        try {
            db.executeQuery(line);
        } catch (const std::exception& e) {
            std::cout << "Runtime Error: " << e.what() << std::endl;
        }
    }

    std::cout << "Exiting. Goodbye!" << std::endl;
    return 0;
}
