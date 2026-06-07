#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cassert>
#include "bplus_tree.hpp"

using StudentRow = std::string;

void runAutomatedTests() {
    std::cout << "\n============================================\n";
    std::cout << "     B+ TREE AUTOMATED TEST SUITE (Lab 6)   \n";
    std::cout << "============================================\n";

    // Test 1: Ascending inserts
    {
        std::cout << "[Test 1] Ascending Inserts (1 to 30): ";
        DB<int, StudentRow> db(2);
        for (int i = 1; i <= 30; ++i) {
            db.insert(i, "ROW_" + std::to_string(i));
            assert(db.validate());
        }
        assert(db.size() == 30);
        for (int i = 1; i <= 30; ++i) {
            auto entry = db.search(i);
            assert(entry.found && entry.row == "ROW_" + std::to_string(i));
        }
        std::cout << "PASSED\n";
    }

    // Test 2: Descending inserts
    {
        std::cout << "[Test 2] Descending Inserts (30 to 1): ";
        DB<int, StudentRow> db(2);
        for (int i = 30; i >= 1; --i) {
            db.insert(i, "ROW_" + std::to_string(i));
            assert(db.validate());
        }
        assert(db.size() == 30);
        std::cout << "PASSED\n";
    }

    // Test 3: Random inserts and point lookups
    {
        std::cout << "[Test 3] Random Inserts + Point Lookups: ";
        DB<int, StudentRow> db(3);
        std::vector<int> keys = {42, 7, 19, 3, 88, 55, 12, 67, 1, 99, 23, 45, 8, 71, 36};
        for (int k : keys) {
            db.insert(k, "DATA_" + std::to_string(k));
            assert(db.validate());
        }
        assert(db.size() == keys.size());
        for (int k : keys) {
            auto entry = db.search(k);
            assert(entry.found && entry.row == "DATA_" + std::to_string(k));
        }
        auto missing = db.search(1000);
        assert(!missing.found);
        std::cout << "PASSED\n";
    }

    // Test 4: Range search via leaf chain
    {
        std::cout << "[Test 4] Range Search [10, 25]: ";
        DB<int, StudentRow> db(2);
        for (int i = 1; i <= 50; ++i) {
            db.insert(i, "VAL_" + std::to_string(i));
        }
        auto results = db.range_search(10, 25);
        assert(results.size() == 16);
        assert(results.front().key == 10);
        assert(results.back().key == 25);
        for (size_t i = 1; i < results.size(); ++i) {
            assert(results[i - 1].key < results[i].key);
        }
        std::cout << "PASSED\n";
    }

    // Test 5: Node splitting under load
    {
        std::cout << "[Test 5] Stress Test (500 inserts, min_degree=2): ";
        DB<int, StudentRow> db(2);
        for (int i = 0; i < 500; ++i) {
            db.insert(i, "STRESS_" + std::to_string(i));
            assert(db.validate());
        }
        assert(db.size() == 500);
        for (int i = 0; i < 500; i += 17) {
            auto entry = db.search(i);
            assert(entry.found);
        }
        std::cout << "PASSED\n";
    }

    std::cout << "All B+ Tree tests completed successfully!\n";
    std::cout << "============================================\n\n";
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--run-tests") {
        runAutomatedTests();
        return 0;
    }

    runAutomatedTests();

    DB<int, StudentRow> db(2);
    std::cout << "Interactive B+ Tree CLI (Lab 6):\n";
    std::cout << "Commands:\n";
    std::cout << "  insert <key> <row>  : Insert key/value pair\n";
    std::cout << "  search <key>        : Search for a key\n";
    std::cout << "  range <low> <high>  : Range search inclusive\n";
    std::cout << "  print               : Print tree structure\n";
    std::cout << "  validate            : Verify B+ tree properties\n";
    std::cout << "  size                : Show total entries\n";
    std::cout << "  demo                : Load sample student records\n";
    std::cout << "  help                : Show command list\n";
    std::cout << "  exit                : Close application\n\n";

    std::string line;
    while (true) {
        std::cout << "bptree> ";
        if (!std::getline(std::cin, line)) break;

        std::stringstream ss(line);
        std::string command;
        ss >> command;

        if (command == "exit") {
            break;
        } else if (command == "insert") {
            int key;
            std::string row;
            if (ss >> key >> row) {
                db.insert(key, row);
                std::cout << "Inserted (" << key << ", " << row << "). Valid: "
                          << (db.validate() ? "YES" : "NO") << "\n";
            } else {
                std::cout << "Usage: insert <key> <row_string>\n";
            }
        } else if (command == "search") {
            int key;
            if (ss >> key) {
                auto entry = db.search(key);
                if (entry.found) {
                    std::cout << "Found key " << key << " -> " << entry.row << "\n";
                } else {
                    std::cout << "Key " << key << " not found.\n";
                }
            } else {
                std::cout << "Usage: search <key>\n";
            }
        } else if (command == "range") {
            int low, high;
            if (ss >> low >> high) {
                auto results = db.range_search(low, high);
                std::cout << "Range [" << low << ", " << high << "] -> "
                          << results.size() << " entries:\n";
                for (const auto& e : results) {
                    std::cout << "  (" << e.key << ", " << e.row << ")\n";
                }
            } else {
                std::cout << "Usage: range <low> <high>\n";
            }
        } else if (command == "print") {
            db.print();
        } else if (command == "validate") {
            std::cout << (db.validate() ? "B+ Tree properties are valid.\n"
                                         : "B+ Tree property violation detected!\n");
        } else if (command == "size") {
            std::cout << "Total entries: " << db.size() << "\n";
        } else if (command == "demo") {
            db.insert(10, "ROW_OF_10");
            db.insert(20, "ROW_OF_20");
            db.insert(40, "ROW_OF_40");
            db.insert(15, "ROW_OF_15");
            db.insert(25, "ROW_OF_25");
            std::cout << "Loaded demo records. Tree size: " << db.size() << "\n";
            db.print();
        } else if (command == "help" || command == "?") {
            std::cout << "Commands: insert, search, range, print, validate, size, demo, exit\n";
        } else if (!command.empty()) {
            std::cout << "Unknown command: '" << command << "'. Type 'help'.\n";
        }
    }

    return 0;
}
