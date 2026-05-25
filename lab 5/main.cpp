#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include "rbt.hpp"

void printHeader() {
    std::cout << "\033[1;36m========================================================\033[0m\n";
    std::cout << "\033[1;36m           LAB 5: RED-BLACK TREE IMPLEMENTATION          \033[0m\n";
    std::cout << "\033[1;36m========================================================\033[0m\n";
}

void showMenu() {
    std::cout << "\n\033[1;32mMenu Options:\033[0m\n";
    std::cout << "  [1] Insert a single key\n";
    std::cout << "  [2] Insert a batch of keys (comma separated)\n";
    std::cout << "  [3] Search for a key\n";
    std::cout << "  [4] Display Tree Structure (Color Visualizer)\n";
    std::cout << "  [5] Run Diagnostics & Compare with AVL Properties\n";
    std::cout << "  [6] Run Automatic Demo Sequence\n";
    std::cout << "  [0] Exit\n";
    std::cout << "\033[1;32mEnter Choice: \033[0m";
}

void runDemo(RedBlackTree& tree) {
    std::cout << "\n\033[1;33m[Demo] Initializing Tree with standard insertion sequence:\033[0m\n";
    std::vector<int> demoKeys = {10, 20, 30, 15, 25, 5, 1};
    std::cout << "Inserting keys: ";
    for (int k : demoKeys) {
        std::cout << k << " ";
        tree.insert(k);
    }
    std::cout << "\nDone.\n";
    
    std::cout << "\n\033[1;33m[Demo] Visualizing Resulting Tree:\033[0m\n";
    tree.printTree();
}

void displayDiagnostics(RedBlackTree& tree) {
    std::cout << "\n\033[1;35m--- TREE DIAGNOSTICS & BALANCING REPORT ---\033[0m\n";
    std::cout << "Total Tree Height: " << tree.getHeight() << "\n";
    std::cout << "Black-Node Height: " << tree.getBlackHeight() << "\n";
    
    std::cout << "Satisfies Red-Black Properties? ";
    if (tree.isRBBalanced()) {
        std::cout << "\033[1;32mYES\033[0m (No consecutive red nodes, consistent black height on all paths)\n";
    } else {
        std::cout << "\033[1;31mNO\033[0m (Red-Black violations detected!)\n";
    }
    
    std::cout << "Satisfies strict AVL Balance (Height Diff <= 1)? ";
    if (tree.isAVLBalanced()) {
        std::cout << "\033[1;32mYES\033[0m (Every node's left/right subtrees differ by at most 1)\n";
    } else {
        std::cout << "\033[1;33mNO\033[0m (Red-Black balancing constraints allow height difference > 1)\n";
    }

    std::cout << "\nNode-by-Node Analysis (In-Order):\n";
    std::cout << "\033[1;30m" << std::setw(10) << "Node Key" << std::setw(15) << "Node Height" << std::setw(20) << "Balance Factor (L-R)" << "\033[0m\n";
    std::cout << "-------------------------------------------------------------\n";
    for (const auto& entry : tree.getNodeHeightsAndBalances()) {
        int key = entry.first;
        int height = entry.second.first;
        int bal = entry.second.second;
        
        std::string balanceAlert = "";
        if (std::abs(bal) > 1) {
            balanceAlert = " \033[1;33m[AVL Height Diff > 1]\033[0m";
        }
        std::cout << std::setw(10) << key << std::setw(15) << height << std::setw(20) << bal << balanceAlert << "\n";
    }
}

int main() {
    printHeader();
    RedBlackTree tree;
    
    // Automatically run demo at startup to populate initial values
    runDemo(tree);
    
    int choice = -1;
    while (choice != 0) {
        showMenu();
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::string discard;
            std::cin >> discard;
            std::cout << "\033[1;31mInvalid input. Please enter a number.\033[0m\n";
            continue;
        }
        
        switch (choice) {
            case 1: {
                int val;
                std::cout << "Enter key to insert: ";
                if (std::cin >> val) {
                    tree.insert(val);
                    std::cout << "Key " << val << " inserted.\n";
                } else {
                    std::cin.clear();
                    std::string dummy;
                    std::cin >> dummy;
                    std::cout << "\033[1;31mInvalid integer.\033[0m\n";
                }
                break;
            }
            case 2: {
                std::string line;
                std::cout << "Enter keys separated by commas (e.g. 5,12,3): ";
                std::cin.ignore();
                std::getline(std::cin, line);
                std::stringstream ss(line);
                int val;
                int count = 0;
                while (ss >> val) {
                    tree.insert(val);
                    count++;
                    if (ss.peek() == ',' || ss.peek() == ' ') {
                        ss.ignore();
                    }
                }
                std::cout << "Inserted " << count << " keys.\n";
                break;
            }
            case 3: {
                int val;
                std::cout << "Enter key to search: ";
                if (std::cin >> val) {
                    Node* res = tree.search(val);
                    if (res != nullptr) {
                        std::string colorName = (res->color == RED) ? "\033[1;31mRED\033[0m" : "\033[1;37mBLACK\033[0m";
                        std::cout << "Found key " << val << " in RBT. Color is " << colorName << ".\n";
                    } else {
                        std::cout << "\033[1;31mKey " << val << " not found in RBT.\033[0m\n";
                    }
                } else {
                    std::cin.clear();
                    std::string dummy;
                    std::cin >> dummy;
                    std::cout << "\033[1;31mInvalid integer.\033[0m\n";
                }
                break;
            }
            case 4:
                std::cout << "\n\033[1;33mCurrent Red-Black Tree Structure:\033[0m\n";
                tree.printTree();
                break;
            case 5:
                displayDiagnostics(tree);
                break;
            case 6: {
                RedBlackTree tempTree;
                runDemo(tempTree);
                displayDiagnostics(tempTree);
                break;
            }
            case 0:
                std::cout << "Exiting demo. Goodbye!\n";
                break;
            default:
                std::cout << "\033[1;31mInvalid option. Please choose a menu item.\033[0m\n";
                break;
        }
    }
    
    return 0;
}
