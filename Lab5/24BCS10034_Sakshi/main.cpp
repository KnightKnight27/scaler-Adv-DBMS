#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <random>
#include <cassert>
#include "rbt.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Automated Test Suite
// ─────────────────────────────────────────────────────────────────────────────

void runAutomatedTests() {
    std::cout << "\n============================================\n";
    std::cout << "       RUNNING AUTOMATED TEST SUITE         \n";
    std::cout << "============================================\n";

    // ── Test 1: Ascending inserts ─────────────────────────────────────────
    // Verifies right-leaning chain balancing (rotations + recoloring).
    {
        std::cout << "[Test 1] Ascending Inserts (1 to 20):                    ";
        RedBlackTree tree;
        for (int i = 1; i <= 20; ++i) {
            tree.insert(i);
            assert(tree.validateProperties() &&
                   "RBT properties violated during ascending insertion");
        }
        std::cout << "PASSED\n";
    }

    // ── Test 2: Descending inserts ────────────────────────────────────────
    // Verifies left-leaning chain balancing.
    {
        std::cout << "[Test 2] Descending Inserts (20 to 1):                   ";
        RedBlackTree tree;
        for (int i = 20; i >= 1; --i) {
            tree.insert(i);
            assert(tree.validateProperties() &&
                   "RBT properties violated during descending insertion");
        }
        std::cout << "PASSED\n";
    }

    // ── Test 3: Deletion cases ────────────────────────────────────────────
    // Covers leaf, single-child, and two-child deletion scenarios.
    {
        std::cout << "[Test 3] Delete Leaf / 1-Child / 2-Children Nodes:       ";
        RedBlackTree tree;
        for (int k : {10, 20, 30, 15, 25, 5, 8, 3})
            tree.insert(k);

        assert(tree.validateProperties());

        tree.remove(3);                 // leaf
        assert(tree.validateProperties() && "Failed after deleting leaf (3)");

        tree.remove(5);                 // 1-child
        assert(tree.validateProperties() && "Failed after deleting 1-child (5)");

        tree.remove(20);                // 2-children
        assert(tree.validateProperties() && "Failed after deleting 2-children (20)");

        std::cout << "PASSED\n";
    }

    // ── Test 4: Stress test ───────────────────────────────────────────────
    // 500 random insertions followed by 300 random deletions.
    {
        std::cout << "[Test 4] Stress Test (500 inserts + 300 deletes, seed=42): ";
        RedBlackTree     tree;
        std::vector<int> activeKeys;

        std::mt19937                     rng(42);           // fixed seed
        std::uniform_int_distribution<int> dist(1, 10000);

        for (int i = 0; i < 500; ++i) {
            int key = dist(rng);
            if (tree.search(key) == tree.getNil()) {
                tree.insert(key);
                activeKeys.push_back(key);
                assert(tree.validateProperties() &&
                       "Failed during random-insertion stress test");
            }
        }

        std::shuffle(activeKeys.begin(), activeKeys.end(), rng);

        for (int i = 0; i < 300 && i < static_cast<int>(activeKeys.size()); ++i) {
            tree.remove(activeKeys[i]);
            assert(tree.validateProperties() &&
                   "Failed during random-deletion stress test");
        }

        std::cout << "PASSED\n";
    }

    std::cout << "\nAll automated tests passed — no property violations found.\n";
    std::cout << "============================================\n\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  CLI Helpers
// ─────────────────────────────────────────────────────────────────────────────

static void printHelp() {
    std::cout <<
        "  insert <val>  — Insert a key\n"
        "  delete <val>  — Remove a key\n"
        "  search <val>  — Search for a key\n"
        "  print         — Display visual tree structure\n"
        "  traverse      — Print pre-, in-, and post-order traversals\n"
        "  validate      — Programmatically verify RBT properties\n"
        "  help          — Show this command list\n"
        "  exit          — Close the application\n";
}

static std::string validityTag(bool ok) {
    return ok ? "\033[1;32mVALID\033[0m" : "\033[1;31mVIOLATED\033[0m";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entry Point
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // --run-tests : execute test suite and exit
    if (argc > 1 && std::string(argv[1]) == "--run-tests") {
        runAutomatedTests();
        return 0;
    }

    // Default: run tests first, then open interactive CLI
    runAutomatedTests();

    RedBlackTree tree;
    std::cout << "Red-Black Tree — Interactive CLI\n";
    std::cout << "─────────────────────────────────\n";
    printHelp();
    std::cout << "\n";

    std::string line;
    while (true) {
        std::cout << "rbt> ";
        if (!std::getline(std::cin, line)) break;

        std::stringstream ss(line);
        std::string       cmd;
        ss >> cmd;

        if (cmd == "exit") {
            break;

        } else if (cmd == "insert") {
            int val;
            if (ss >> val) {
                tree.insert(val);
                std::cout << "Inserted " << val
                          << " — properties: " << validityTag(tree.validateProperties()) << "\n";
            } else {
                std::cout << "Usage: insert <integer>\n";
            }

        } else if (cmd == "delete") {
            int val;
            if (ss >> val) {
                tree.remove(val);
                std::cout << "Deleted " << val
                          << " — properties: " << validityTag(tree.validateProperties()) << "\n";
            } else {
                std::cout << "Usage: delete <integer>\n";
            }

        } else if (cmd == "search") {
            int val;
            if (ss >> val) {
                auto result = tree.search(val);
                if (result != tree.getNil()) {
                    std::cout << "Found " << val
                              << " (color: " << (result->color == RED ? "RED" : "BLACK") << ")\n";
                } else {
                    std::cout << "Key " << val << " not found.\n";
                }
            } else {
                std::cout << "Usage: search <integer>\n";
            }

        } else if (cmd == "print") {
            tree.printTree();

        } else if (cmd == "traverse") {
            std::cout << "In-order:   "; tree.inorder();
            std::cout << "Pre-order:  "; tree.preorder();
            std::cout << "Post-order: "; tree.postorder();

        } else if (cmd == "validate") {
            if (tree.validateProperties()) {
                std::cout << "\033[1;32mTree is fully compliant "
                             "(Rules 1–5 + BST property verified)\033[0m\n";
            } else {
                std::cout << "\033[1;31mTree violates one or more "
                             "Red-Black Tree properties!\033[0m\n";
            }

        } else if (cmd == "help" || cmd == "?") {
            printHelp();

        } else if (!cmd.empty()) {
            std::cout << "Unknown command '" << cmd << "'. Type 'help'.\n";
        }
    }

    return 0;
}