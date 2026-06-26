// ============================================================================
// Driver File: SQL Engine Demo
// ============================================================================

#include "sql_engine.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

using namespace mini_sql;

namespace
{

    void printSectionHeading(const std::string &heading)
    {
        std::cout << "\n========================================\n"
                  << " " << heading
                  << "\n========================================\n";
    }

    void validateTest(bool passed, const std::string &failureReason)
    {
        if (!passed)
        {
            std::cerr << "\n[CRITICAL FAILURE] " << failureReason << "\n";
            std::exit(EXIT_FAILURE);
        }
    }

    Relation generateSampleInventory()
    {
        Relation db;
        db.table_name = "smartphones";
        db.headers = {"id", "model", "os", "ram", "price"};

        auto insertPhone = [&](long long id,
                               const std::string &model,
                               const std::string &os,
                               long long ram,
                               long long price)
        {
            db.records.push_back(Tuple{{CellValue{id}, CellValue{model}, CellValue{os}, CellValue{ram}, CellValue{price}}});
        };

        // Mathematically mirrored to match the 8 employee records
        insertPhone(1, "Galaxy S24", "Android", 12, 85000);
        insertPhone(2, "iPhone 15", "iOS", 6, 75000);
        insertPhone(3, "OnePlus 12", "Android", 16, 65000);
        insertPhone(4, "iPhone 13", "iOS", 4, 50000);
        insertPhone(5, "iPhone 15 Pro", "iOS", 12, 130000);
        insertPhone(6, "Edge 50", "Android", 12, 35000);
        insertPhone(7, "Nothing Phone 2", "Android", 12, 45000);
        insertPhone(8, "X100 Pro", "Android", 16, 90000);

        return db;
    }

} // anonymous namespace

int main()
{
    printSectionHeading("Test 1: Dijkstra Shunting-Yard (Infix -> RPN)");

    const std::vector<std::string> testClauses = {
        "ram > 8 AND (os = 'iOS' OR price >= 80000)",
        "NOT ram < 12 AND price > 90000",
        "id = 1 OR id = 3 OR id = 8"};

    for (const auto &clause : testClauses)
    {
        auto rpnStack = shuntingYardRPN(tokenizeSQL(clause));
        std::cout << "  Raw WHERE Clause : " << clause << "\n";
        std::cout << "  Compiled Postfix : " << stringifyRPN(rpnStack) << "\n\n";
    }

    Relation inventoryTable = generateSampleInventory();

    printSectionHeading("Test 2A: Unfiltered 'SELECT *'");
    {
        Relation res = executeQuery(compileQuery("SELECT * FROM smartphones"), inventoryTable);
        printTable(res, std::cout);
        validateTest(res.records.size() == 8, "Failed: Expected full 8 row dump.");
    }

    printSectionHeading("Test 2B: Compound WHERE + ORDER BY DESC");
    {
        std::string queryB =
            "SELECT model, os, price FROM smartphones "
            "WHERE ram > 8 AND (os = 'iOS' OR price >= 80000) "
            "ORDER BY price DESC";

        std::cout << "Executing: [" << queryB << "]\n";
        Relation res = executeQuery(compileQuery(queryB), inventoryTable);
        printTable(res, std::cout);

        validateTest(res.records.size() == 3, "Failed: Filter should catch exactly 3 phones.");
        validateTest(std::get<std::string>(res.records[0].values[0]) == "iPhone 15 Pro", "Failed: Highest price must be iPhone 15 Pro.");
        validateTest(std::get<std::string>(res.records[2].values[0]) == "Galaxy S24", "Failed: Lowest price of the 3 must be S24.");
    }

    printSectionHeading("Test 2C: Unary NOT + ORDER BY ASC + LIMIT");
    {
        std::string queryC =
            "SELECT model, ram FROM smartphones "
            "WHERE NOT os = 'Android' "
            "ORDER BY ram ASC LIMIT 2";

        std::cout << "Executing: [" << queryC << "]\n";
        Relation res = executeQuery(compileQuery(queryC), inventoryTable);
        printTable(res, std::cout);

        validateTest(res.records.size() == 2, "Failed: LIMIT 2 failed to throttle output.");
        validateTest(std::get<std::string>(res.records[0].values[0]) == "iPhone 13", "Failed: Lowest RAM iOS device is iPhone 13.");
        validateTest(std::get<long long>(res.records[1].values[1]) == 6, "Failed: 2nd lowest RAM must be 6GB.");
    }

    printSectionHeading("Test 2D: Low-Level Row Stack Evaluator");
    {
        auto rpnPredicate = shuntingYardRPN(tokenizeSQL("os = 'Android' AND price >= 85000"));
        std::cout << "Target RPN: " << stringifyRPN(rpnPredicate) << "\n";

        size_t confirmedHits = 0;
        for (const auto &row : inventoryTable.records)
        {
            if (evaluateRPN(rpnPredicate, inventoryTable, row))
            {
                confirmedHits++;
                std::cout << "  Passed Predicate: " << std::get<std::string>(row.values[1]) << "\n";
            }
        }
        validateTest(confirmedHits == 2, "Failed: Stack evaluator should have matched 2 Androids.");
    }

    std::cout << "\n>>> ALL PROCESSING ENGINE TESTS PASSED <<<\n";
    return EXIT_SUCCESS;
}