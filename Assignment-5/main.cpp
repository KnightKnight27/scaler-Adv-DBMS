// main.cpp — Lab 5
// Tanishq Singh | 24BCS10303
//
// Interactive CLI for both the B-Tree and Red-Black Tree.
// Pre-seeds each structure with a handful of values so there's something
// to look at immediately, then drops into a menu loop.

#include <iostream>
#include <string>
#include <limits>
#include "btree.hpp"
#include "rbt.hpp"

static void divider() {
    std::cout << "\n---------------------------------------------\n";
}

static int readInt(const std::string& prompt) {
    int val;
    while (true) {
        std::cout << prompt;
        if (std::cin >> val) return val;
        if (std::cin.eof()) return 0;
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "That wasn't a number, try again.\n";
    }
}

// ---- B-Tree menu ----
static void btreeMenu(BTree& bt) {
    int choice = -1;
    while (choice != 0) {
        divider();
        std::cout << "  B-TREE\n";
        std::cout << "  1. Insert key\n";
        std::cout << "  2. Search key\n";
        std::cout << "  3. Print tree\n";
        std::cout << "  4. In-order traversal\n";
        std::cout << "  0. Back\n";
        choice = readInt("  > ");

        if (choice == 1) {
            int k = readInt("  key: ");
            bt.insert(k);
            std::cout << "  inserted " << k << "\n";
        } else if (choice == 2) {
            int k = readInt("  key: ");
            BTreeNode* found = bt.search(k);
            if (found) {
                std::cout << "  found in node: [";
                for (int i = 0; i < found->n; i++)
                    std::cout << found->keys[i] << (i == found->n - 1 ? "" : ", ");
                std::cout << "]\n";
            } else {
                std::cout << "  not found\n";
            }
        } else if (choice == 3) {
            std::cout << "\n";
            bt.printTree();
        } else if (choice == 4) {
            std::cout << "  in-order:";
            bt.traverse();
        } else if (choice == 0) {
            std::cout << "  back.\n";
        } else {
            std::cout << "  invalid option\n";
        }
    }
}

// ---- RBT menu ----
static void rbtMenu(RedBlackTree& rbt) {
    int choice = -1;
    while (choice != 0) {
        divider();
        std::cout << "  RED-BLACK TREE\n";
        std::cout << "  1. Insert key\n";
        std::cout << "  2. Search key\n";
        std::cout << "  3. Print tree\n";
        std::cout << "  4. Tree stats (height, black-height, balance checks)\n";
        std::cout << "  0. Back\n";
        choice = readInt("  > ");

        if (choice == 1) {
            int k = readInt("  key: ");
            rbt.insert(k);
            std::cout << "  inserted " << k << "\n";
        } else if (choice == 2) {
            int k = readInt("  key: ");
            RBTNode* found = rbt.search(k);
            if (found) {
                std::string col = (found->color == RED) ? "RED" : "BLACK";
                std::cout << "  found: key=" << k << " color=" << col << "\n";
            } else {
                std::cout << "  not found\n";
            }
        } else if (choice == 3) {
            std::cout << "\n";
            rbt.printTree();
        } else if (choice == 4) {
            std::cout << "  height:        " << rbt.getHeight() << "\n";
            std::cout << "  black-height:  " << rbt.getBlackHeight() << "\n";
            std::cout << "  RB-balanced:   " << (rbt.isRBBalanced() ? "yes" : "NO — violation!") << "\n";
            std::cout << "  AVL-balanced:  " << (rbt.isAVLBalanced() ? "yes" : "no (that's fine for RBT)") << "\n";
        } else if (choice == 0) {
            std::cout << "  back.\n";
        } else {
            std::cout << "  invalid option\n";
        }
    }
}

int main() {
    std::cout << "=============================================\n";
    std::cout << "  Lab 5 — B-Tree & Red-Black Tree\n";
    std::cout << "  Tanishq Singh | 24BCS10303\n";
    std::cout << "=============================================\n";

    // seed the B-Tree (t=3, so nodes hold 2–5 keys)
    BTree bt(3);
    for (int k : {10, 20, 5, 6, 12, 30, 7, 17, 3, 25, 40, 50, 22})
        bt.insert(k);

    // seed the Red-Black Tree
    RedBlackTree rbt;
    for (int k : {10, 20, 30, 15, 25, 5, 1, 35, 8})
        rbt.insert(k);

    int choice = -1;
    while (choice != 0) {
        divider();
        std::cout << " 1. B-Tree\n";
        std::cout << " 2. Red-Black Tree\n";
        std::cout << " 0. Exit\n";
        choice = readInt(" > ");

        if (choice == 1)      btreeMenu(bt);
        else if (choice == 2) rbtMenu(rbt);
        else if (choice == 0) std::cout << " bye.\n";
        else std::cout << " pick 1, 2, or 0\n";
    }

    return 0;
}
