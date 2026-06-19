// ============================================================
// DoraDB — Main Entry Point (REPL + Tests)
//
// Usage:
//   ./doradb           → interactive REPL
//   ./doradb --test    → run M3 integration tests
// ============================================================

#include "storage/heap_engine.h"
#include "parser/tokenizer.h"
#include "parser/parser.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <sstream>

// ============================================================
// Execute a SQL script file (\i command)
// ============================================================
static void ExecuteFile(HeapEngine& engine, const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "Error: cannot open file '" << filename << "'\n";
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '-') continue;  // skip empty/comments
        std::cout << "  > " << line << "\n";
        try {
            auto tokens = Tokenizer(line).Tokenize();
            auto stmt = Parser(tokens).Parse();
            std::string result = engine.Execute(stmt);
            std::cout << result << "\n";
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }
}

// ============================================================
// Execute one SQL line
// ============================================================
static void ExecuteLine(HeapEngine& engine, const std::string& line) {
    auto tokens = Tokenizer(line).Tokenize();
    auto stmt = Parser(tokens).Parse();
    std::string result = engine.Execute(stmt);
    std::cout << result << "\n";
}

// ============================================================
// Integration tests for M3
// ============================================================
static int tests_passed = 0, tests_total = 0;

void CHECK(bool cond, const std::string& msg) {
    tests_total++;
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << msg << "\n";
    if (cond) tests_passed++;
}

static int RunTests() {
    std::cout << "========================================\n";
    std::cout << "  DoraDB — Milestone 3 Tests\n";
    std::cout << "  Query Execution + Optimizer\n";
    std::cout << "========================================\n";

    std::filesystem::remove_all("test_data");
    {
        HeapEngine engine("test_data");

        // CREATE TABLE
        std::cout << "\n=== CREATE TABLE ===\n";
        ExecuteLine(engine, "CREATE TABLE students (id INT, name VARCHAR(50), active BOOL, PRIMARY KEY(id));");
        CHECK(engine.GetCatalog().GetTable("students") != nullptr, "table created");

        // INSERT
        std::cout << "\n=== INSERT ===\n";
        ExecuteLine(engine, "INSERT INTO students VALUES (1, 'Alice', true);");
        ExecuteLine(engine, "INSERT INTO students VALUES (2, 'Bob', false);");
        ExecuteLine(engine, "INSERT INTO students VALUES (3, 'Charlie', true);");
        ExecuteLine(engine, "INSERT INTO students VALUES (4, 'Diana', false);");
        ExecuteLine(engine, "INSERT INTO students VALUES (5, 'Eve', true);");
        CHECK(engine.GetStats("students").row_count == 5, "5 rows inserted");

        // SELECT *
        std::cout << "\n=== SELECT * ===\n";
        auto rows = engine.Scan("students");
        CHECK((int)rows.size() == 5, "scan returns 5 rows");

        // SELECT with WHERE (index scan)
        std::cout << "\n=== SELECT WHERE id = 3 (IndexScan) ===\n";
        ExecuteLine(engine, "SELECT * FROM students WHERE id = 3;");
        auto r = engine.Get("students", 3);
        CHECK(r.size() == 1 && r[0][1].str_val == "Charlie", "index lookup: Charlie");

        // SELECT with range (should use index)
        std::cout << "\n=== SELECT WHERE id > 2 AND id <= 4 ===\n";
        ExecuteLine(engine, "SELECT * FROM students WHERE id > 2 AND id <= 4;");

        // SELECT with non-PK filter (SeqScan + Filter)
        std::cout << "\n=== SELECT WHERE active = true (SeqScan) ===\n";
        ExecuteLine(engine, "SELECT * FROM students WHERE active = true;");

        // SELECT specific columns
        std::cout << "\n=== SELECT name FROM students ===\n";
        ExecuteLine(engine, "SELECT name FROM students WHERE id = 1;");

        // UPDATE
        std::cout << "\n=== UPDATE ===\n";
        ExecuteLine(engine, "UPDATE students SET name = 'Alicia' WHERE id = 1;");
        r = engine.Get("students", 1);
        CHECK(r.size() == 1 && r[0][1].str_val == "Alicia", "update: Alice → Alicia");

        // DELETE
        std::cout << "\n=== DELETE ===\n";
        ExecuteLine(engine, "DELETE FROM students WHERE id = 5;");
        CHECK(engine.Get("students", 5).empty(), "Eve deleted");
        CHECK(engine.GetStats("students").row_count == 4, "4 rows remain");

        // JOIN test
        std::cout << "\n=== JOIN ===\n";
        ExecuteLine(engine, "CREATE TABLE courses (cid INT, student_id INT, course VARCHAR(30), PRIMARY KEY(cid));");
        ExecuteLine(engine, "INSERT INTO courses VALUES (1, 1, 'DBMS');");
        ExecuteLine(engine, "INSERT INTO courses VALUES (2, 2, 'OS');");
        ExecuteLine(engine, "INSERT INTO courses VALUES (3, 1, 'Networks');");
        ExecuteLine(engine, "INSERT INTO courses VALUES (4, 3, 'Algorithms');");

        ExecuteLine(engine, "SELECT * FROM students JOIN courses ON students.id = courses.student_id;");

        // Verify join results
        // Alicia(1) should match DBMS and Networks, Bob(2) matches OS, Charlie(3) matches Algorithms
        // Diana(4) has no match

        std::cout << "\n=== Persistence ===\n";
    }

    // Persistence test: reopen engine
    {
        HeapEngine engine("test_data");
        auto r = engine.Get("students", 2);
        CHECK(r.size() == 1 && r[0][1].str_val == "Bob", "Bob survives restart");
        CHECK(engine.Get("students", 5).empty(), "Eve still deleted after restart");
        CHECK(engine.GetStats("students").row_count == 4, "row count correct after restart");
    }

    std::cout << "\n========================================\n";
    std::cout << "  Results: " << tests_passed << "/" << tests_total << " passed\n";
    std::cout << "========================================\n";

    std::filesystem::remove_all("test_data");
    return (tests_passed == tests_total) ? 0 : 1;
}

// ============================================================
// REPL
// ============================================================
static void RunREPL() {
    std::cout << "DoraDB v0.3 — A MiniDB Engine\n";
    std::cout << "Type SQL statements, \\i <file> to run script, \\q to quit.\n\n";

    HeapEngine engine("data");

    while (true) {
        std::cout << "DoraDB> " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) break;

        // Trim
        while (!line.empty() && std::isspace(line.front())) line.erase(line.begin());
        while (!line.empty() && std::isspace(line.back())) line.pop_back();
        if (line.empty()) continue;

        // Meta-commands
        if (line == "\\q") { std::cout << "Bye!\n"; break; }
        if (line.starts_with("\\i ")) { ExecuteFile(engine, line.substr(3)); continue; }
        if (line == "\\dt") {
            for (auto& name : engine.GetCatalog().GetAllTableNames()) {
                auto* info = engine.GetCatalog().GetTable(name);
                std::cout << "  " << name << " (" << info->schema.columns.size() << " columns, "
                          << engine.GetStats(name).row_count << " rows)\n";
            }
            continue;
        }

        try {
            ExecuteLine(engine, line);
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--test") {
        return RunTests();
    }
    RunREPL();
    return 0;
}
