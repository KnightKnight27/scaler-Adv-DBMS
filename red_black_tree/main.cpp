#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <random>
#include <cassert>
#include "rbt.hpp"

// Run automated test suite
void runAutomatedTests() {
    std::cout << "\n============================================\n";
    std::cout << "        RUNNING AUTOMATED TEST SUITE        \n";
    std::cout << "============================================\n";

    // Test 1: Ascending inserts (checks right-leaning chain balancing)
    {
        std::cout << "[Test 1] Ascending Inserts (1 to 20): ";
        RedBlackTree tree;
        for (int i = 1; i <= 20; ++i) {
            tree.insert(i);
            assert(tree.validateProperties() && "RBT properties violated during ascending insertion");
        }
        std::cout << "PASSED\n";
    }

    // Test 2: Descending inserts (checks left-leaning chain balancing)
    {
        std::cout << "[Test 2] Descending Inserts (20 to 1): ";
        RedBlackTree tree;
        for (int i = 20; i >= 1; --i) {
            tree.insert(i);
            assert(tree.validateProperties() && "RBT properties violated during descending insertion");
        }
        std::cout << "PASSED\n";
    }

    // Test 3: Basic deletion cases
    {
        std::cout << "[Test 3] Insert & Deletion of Nodes (leaves, 1-child, 2-children): ";
        RedBlackTree tree;
        std::vector<int> keys = {10, 20, 30, 15, 25, 5, 8, 3};
        for (int k : keys) {
            tree.insert(k);
        }
        assert(tree.validateProperties());

        // Delete leaf node (3)
        tree.remove(3);
        assert(tree.validateProperties() && "Failed after deleting leaf node 3");

        // Delete node with 1 child (5)
        tree.remove(5);
        assert(tree.validateProperties() && "Failed after deleting node with 1 child (5)");

        // Delete node with 2 children (20)
        tree.remove(20);
        assert(tree.validateProperties() && "Failed after deleting node with 2 children (20)");

        std::cout << "PASSED\n";
    }

    // Test 4: Heavy Stress Test (Random insertions and deletions)
    {
        std::cout << "[Test 4] Stress Test (1000 random operations): ";
        RedBlackTree tree;
        std::vector<int> active_keys;
        
        // Random engine
        std::mt19937 rng(42); // fixed seed for reproducibility
        std::uniform_int_distribution<int> dist(1, 10000);

        // 500 Random Insertions
        for (int i = 0; i < 500; ++i) {
            int key = dist(rng);
            if (tree.search(key) == tree.getNil()) {
                tree.insert(key);
                active_keys.push_back(key);
                assert(tree.validateProperties() && "Failed during random insertions stress test");
            }
        }

        // Shuffle active keys to delete in random order
        std::shuffle(active_keys.begin(), active_keys.end(), rng);

        // Delete 300 of the keys
        for (int i = 0; i < 300 && i < active_keys.size(); ++i) {
            tree.remove(active_keys[i]);
            assert(tree.validateProperties() && "Failed during random deletions stress test");
        }

        std::cout << "PASSED\n";
    }

    std::cout << "All automated tests completed successfully! No violations found.\n";
    std::cout << "============================================\n\n";
}

int main(int argc, char* argv[]) {
    // If run-tests argument is specified, only run tests
    if (argc > 1 && std::string(argv[1]) == "--run-tests") {
        runAutomatedTests();
        return 0;
    }

    // By default, run tests first to demonstrate correctness, then enter CLI
    runAutomatedTests();

    RedBlackTree tree;
    std::cout << "Interactive Red-Black Tree CLI:\n";
    std::cout << "Commands:\n";
    std::cout << "  insert <val>  : Insert a key\n";
    std::cout << "  delete <val>  : Remove a key\n";
    std::cout << "  search <val>  : Search for a key\n";
    std::cout << "  print         : Display visual tree structure\n";
    std::cout << "  traverse      : Print pre, in, and post-order traversals\n";
    std::cout << "  validate      : Programmatically verify RBT properties\n";
    std::cout << "  help          : Show command list\n";
    std::cout << "  exit          : Close application\n\n";

    std::string line;
    while (true) {
        std::cout << "rbt> ";
        if (!std::getline(std::cin, line)) {
            break;
        }

        std::stringstream ss(line);
        std::string command;
        ss >> command;

        if (command == "exit") {
            break;
        } else if (command == "insert") {
            int val;
            if (ss >> val) {
                tree.insert(val);
                std::cout << "Inserted " << val << ". Tree properties: " 
                          << (tree.validateProperties() ? "\033[1;32mVALID\033[0m" : "\033[1;31mVIOLATED\033[0m") 
                          << "\n";
            } else {
                std::cout << "Usage: insert <integer>\n";
            }
        } else if (command == "delete") {
            int val;
            if (ss >> val) {
                tree.remove(val);
                std::cout << "Deleted " << val << ". Tree properties: " 
                          << (tree.validateProperties() ? "\033[1;32mVALID\033[0m" : "\033[1;31mVIOLATED\033[0m") 
                          << "\n";
            } else {
                std::cout << "Usage: delete <integer>\n";
            }
        } else if (command == "search") {
            int val;
            if (ss >> val) {
                auto result = tree.search(val);
                if (result != tree.getNil()) {
                    std::string colorStr = (result->color == RED) ? "RED" : "BLACK";
                    std::cout << "Found key " << val << " with color: " << colorStr << "\n";
                } else {
                    std::cout << "Key " << val << " not found in the tree.\n";
                }
            } else {
                std::cout << "Usage: search <integer>\n";
            }
        } else if (command == "print") {
            tree.printTree();
        } else if (command == "traverse") {
            std::cout << "In-order:   ";
            tree.inorder();
            std::cout << "Pre-order:  ";
            tree.preorder();
            std::cout << "Post-order: ";
            tree.postorder();
        } else if (command == "validate") {
            if (tree.validateProperties()) {
                std::cout << "\033[1;32mTree properties are fully compliant (Rule 1-5 & BST Property verified)\033[0m\n";
            } else {
                std::cout << "\033[1;31mTree violates one or more Red-Black Tree properties!\033[0m\n";
            }
        } else if (command == "help" || command == "?") {
            std::cout << "Commands: insert <val>, delete <val>, search <val>, print, traverse, validate, exit\n";
        } else if (!command.empty()) {
            std::cout << "Unknown command: '" << command << "'. Type 'help' for instructions.\n";
        }
    }

    return 0;
}
