#include "btree.hpp"
#include <iostream>
#include <limits>

void clearInput() {
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

void runDemoSequence() {
    std::cout << "\n==================================================\n";
    std::cout << "          RUNNING B-TREE DEMO SEQUENCE            \n";
    std::cout << "==================================================\n";
    
    int t = 3;
    std::cout << "Creating B-Tree with minimum degree t = " << t << "...\n";
    std::cout << "(Maximum keys per node: " << (2 * t - 1) 
              << ", Min keys per node (except root): " << (t - 1) << ")\n\n";
    
    BTree tree(t);

    // Insert keys: 10, 20, 30, 40, 50, 60, 70, 80, 90, 5, 15, 25
    int insertKeys[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 5, 15, 25};
    std::cout << "Inserting keys:";
    for (int key : insertKeys) {
        std::cout << " " << key;
        tree.insert(key);
    }
    std::cout << "\n\nInsertion complete!\n";

    std::cout << "\n1. In-order Traversal (Should print in sorted order):\n";
    std::cout << "Traversal:";
    tree.traverse();
    std::cout << "\n";

    std::cout << "\n2. Visual Tree Structure:\n";
    tree.printVisual();

    std::cout << "\n3. Searching for keys:\n";
    int searchKeys[] = {30, 15, 99, 5};
    for (int key : searchKeys) {
        int index = -1;
        BTreeNode* foundNode = tree.search(key, index);
        if (foundNode != nullptr) {
            std::cout << "  - Key " << key << " FOUND in node containing " << index << "-th key.\n";
        } else {
            std::cout << "  - Key " << key << " NOT FOUND in the tree.\n";
        }
    }
    std::cout << "==================================================\n\n";
}

int main() {
    std::cout << "==================================================\n";
    std::cout << "            C++ B-TREE IMPLEMENTATION             \n";
    std::cout << "==================================================\n";

    std::cout << "Do you want to run the automated demo first? (y/n): ";
    char choice;
    std::cin >> choice;
    if (choice == 'y' || choice == 'Y') {
        runDemoSequence();
    }

    int t = 3;
    std::cout << "Enter the minimum degree (t) for your interactive B-Tree (t >= 2): ";
    while (true) {
        if (std::cin >> t && t >= 2) {
            break;
        } else {
            std::cout << "Invalid degree. Please enter an integer >= 2: ";
            clearInput();
        }
    }

    BTree tree(t);
    std::cout << "\nCreated B-Tree with minimum degree t = " << t << ".\n";

    int menuChoice;
    while (true) {
        std::cout << "\n--- B-Tree Operations Menu ---\n";
        std::cout << "1. Insert Element\n";
        std::cout << "2. Search Element\n";
        std::cout << "3. In-Order Traversal\n";
        std::cout << "4. Visual Tree representation\n";
        std::cout << "5. Run automated demo sequence again\n";
        std::cout << "6. Exit\n";
        std::cout << "Enter choice (1-6): ";

        if (!(std::cin >> menuChoice)) {
            std::cout << "Invalid choice. Please enter a number between 1 and 6.\n";
            clearInput();
            continue;
        }

        if (menuChoice == 6) {
            std::cout << "Exiting B-Tree interactive session. Goodbye!\n";
            break;
        }

        switch (menuChoice) {
            case 1: {
                int val;
                std::cout << "Enter integer to insert: ";
                if (std::cin >> val) {
                    tree.insert(val);
                    std::cout << "Key " << val << " inserted successfully.\n";
                } else {
                    std::cout << "Invalid input. Insertion aborted.\n";
                    clearInput();
                }
                break;
            }
            case 2: {
                int val;
                std::cout << "Enter integer to search: ";
                if (std::cin >> val) {
                    int index = -1;
                    BTreeNode* res = tree.search(val, index);
                    if (res != nullptr) {
                        std::cout << "Key " << val << " is present in the tree!\n";
                    } else {
                        std::cout << "Key " << val << " is NOT present in the tree.\n";
                    }
                } else {
                    std::cout << "Invalid input.\n";
                    clearInput();
                }
                break;
            }
            case 3:
                std::cout << "In-Order Traversal: ";
                if (tree.isEmpty()) {
                    std::cout << "[Empty Tree]";
                } else {
                    tree.traverse();
                }
                std::cout << "\n";
                break;
            case 4:
                std::cout << "Visual Tree Representation:\n";
                tree.printVisual();
                break;
            case 5:
                runDemoSequence();
                break;
            default:
                std::cout << "Invalid choice. Please select from 1 to 6.\n";
                break;
        }
    }

    return 0;
}
