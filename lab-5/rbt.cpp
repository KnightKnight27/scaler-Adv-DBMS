#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include "rbt.hpp"
#include "btree.hpp"

// =====================================================================
// SIMPLIFIED driver program for Lab 5: Red-Black Trees & B-Trees
// This file runs an interactive console session allowing operations
// on both data structures with clean, easy-to-understand menu options.
// =====================================================================

// Helper function to print decorative dividers
void printLine() {
    std::cout << "\n=============================================\n";
}

// Simple Red-Black Tree console workspace
void handleRedBlackTree(RedBlackTree& rbt) {
    int choice = -1;
    while (choice != 0) {
        printLine();
        std::cout << "        RED-BLACK TREE INTERACTIVE MENU\n";
        std::cout << "1. Insert a single integer key\n";
        std::cout << "2. Search for an integer key\n";
        std::cout << "3. Print current Red-Black Tree structure\n";
        std::cout << "4. Verify Red-Black balance & height diagnostics\n";
        std::cout << "0. Return to main menu\n";
        std::cout << "Enter your choice: ";

        // Read choice and handle invalid characters
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::string discard;
            std::cin >> discard;
            std::cout << "Invalid input. Please enter a valid number.\n";
            continue;
        }

        switch (choice) {
            case 1: {
                // Key insertion operation
                int key;
                std::cout << "Enter integer key to insert: ";
                if (std::cin >> key) {
                    rbt.insert(key); // Inserts and automatically triggers fixup & balancing
                    std::cout << "Inserted " << key << " successfully.\n";
                } else {
                    std::cin.clear();
                    std::string dummy;
                    std::cin >> dummy;
                    std::cout << "Invalid integer format.\n";
                }
                break;
            }
            case 2: {
                // Key search operation
                int key;
                std::cout << "Enter key to search: ";
                if (std::cin >> key) {
                    RBTNode* found = rbt.search(key); // Standard BST search
                    if (found != nullptr) {
                        // Display search result with node color info
                        std::string col = (found->color == COLOR_RED) ? "RED" : "BLACK";
                        std::cout << "Key " << key << " exists in the RBT! Node Color: " << col << "\n";
                    } else {
                        std::cout << "Key " << key << " was not found in the RBT.\n";
                    }
                } else {
                    std::cin.clear();
                    std::string dummy;
                    std::cin >> dummy;
                    std::cout << "Invalid integer format.\n";
                }
                break;
            }
            case 3:
                // Visualize the tree
                std::cout << "\nCurrent Red-Black Tree Structure:\n";
                rbt.printTree();
                break;
            case 4:
                // Audit standard RBT vs AVL balances
                std::cout << "\n--- RED-BLACK TREE PROPERTIES AUDIT ---\n";
                std::cout << "Max Tree Height: " << rbt.getHeight() << "\n";
                std::cout << "Path Black Height: " << rbt.getBlackHeight() << "\n";
                
                std::cout << "Satisfies Red-Black Balance? ";
                if (rbt.isRBBalanced()) {
                    std::cout << "YES (Uniform black heights & no adjacent red nodes)\n";
                } else {
                    std::cout << "NO (Rules violated!)\n";
                }
                
                std::cout << "Satisfies strict AVL Balance (Height Diff <= 1)? ";
                if (rbt.isAVLBalanced()) {
                    std::cout << "YES (Tightly balanced)\n";
                } else {
                    std::cout << "NO (Allowed in RBT since height bounds are looser)\n";
                }
                break;
            case 0:
                std::cout << "Returning to main menu.\n";
                break;
            default:
                std::cout << "Invalid choice. Please select from options 0-4.\n";
                break;
        }
    }
}

// Simple B-Tree console workspace (implements the Lab 5 assignment)
void handleBTree(BTree& btree) {
    int choice = -1;
    while (choice != 0) {
        printLine();
        std::cout << "            B-TREE INTERACTIVE MENU\n";
        std::cout << "1. Insert a single integer key (assignment focus)\n";
        std::cout << "2. Search for an integer key\n";
        std::cout << "3. Print current B-Tree node hierarchy\n";
        std::cout << "4. Print sorted in-order traversal\n";
        std::cout << "0. Return to main menu\n";
        std::cout << "Enter your choice: ";

        // Read choice and handle invalid characters
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::string discard;
            std::cin >> discard;
            std::cout << "Invalid input. Please enter a valid number.\n";
            continue;
        }

        switch (choice) {
            case 1: {
                // Key insertion operation
                int key;
                std::cout << "Enter integer key to insert: ";
                if (std::cin >> key) {
                    btree.insert(key); // Triggers insertion slot search & potential splits
                    std::cout << "Inserted " << key << " successfully.\n";
                } else {
                    std::cin.clear();
                    std::string dummy;
                    std::cin >> dummy;
                    std::cout << "Invalid integer format.\n";
                }
                break;
            }
            case 2: {
                // Key search operation
                int key;
                std::cout << "Enter key to search: ";
                if (std::cin >> key) {
                    BTreeNode* found = btree.search(key); // B-Tree interval-range search
                    if (found != nullptr) {
                        std::cout << "Key " << key << " exists in the B-Tree within node containing keys: [";
                        for (int j = 0; j < found->n; j++) {
                            std::cout << found->keys[j] << (j == found->n - 1 ? "" : ", ");
                        }
                        std::cout << "]\n";
                    } else {
                        std::cout << "Key " << key << " was not found in the B-Tree.\n";
                    }
                } else {
                    std::cin.clear();
                    std::string dummy;
                    std::cin >> dummy;
                    std::cout << "Invalid integer format.\n";
                }
                break;
            }
            case 3:
                // Visualize the tree
                std::cout << "\nCurrent B-Tree Node Hierarchy:\n";
                btree.printTree();
                break;
            case 4:
                // In-order traversal output
                std::cout << "\nB-Tree Sorted Keys (In-Order):";
                btree.traverse();
                break;
            case 0:
                std::cout << "Returning to main menu.\n";
                break;
            default:
                std::cout << "Invalid choice. Please select from options 0-4.\n";
                break;
        }
    }
}

int main() {
    std::cout << "=============================================\n";
    std::cout << "       WELCOME TO ADVANCED INDEXING LAB\n";
    std::cout << "=============================================\n";

    // Instantiate and pre-seed Red-Black Tree with standard keys
    RedBlackTree rbt;
    rbt.insert(10);
    rbt.insert(20);
    rbt.insert(30);
    rbt.insert(15);
    rbt.insert(25);
    rbt.insert(5);
    rbt.insert(1);

    // Instantiate and pre-seed B-Tree (Minimum degree t = 3)
    BTree btree(3);
    btree.insert(10);
    btree.insert(20);
    btree.insert(30);
    btree.insert(40);
    btree.insert(50);
    btree.insert(6);
    btree.insert(17);
    btree.insert(22);

    int choice = -1;
    while (choice != 0) {
        std::cout << "\nChoose the active indexing data structure workspace:\n";
        std::cout << "1. Red-Black Tree Workspace (Self-Balancing BST)\n";
        std::cout << "2. B-Tree Workspace (Multi-Way Disk Index - Assignment Task)\n";
        std::cout << "0. Exit program\n";
        std::cout << "Enter Choice: ";

        // Read choice and handle invalid characters
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::string discard;
            std::cin >> discard;
            std::cout << "Invalid choice. Please enter a valid number.\n";
            continue;
        }

        switch (choice) {
            case 1:
                handleRedBlackTree(rbt);
                break;
            case 2:
                handleBTree(btree);
                break;
            case 3:
            case 0:
                std::cout << "\nExiting lab program. Session complete.\n";
                choice = 0;
                break;
            default:
                std::cout << "Option not found. Choose 1, 2, or 0.\n";
                break;
        }
    }

    return 0;
}