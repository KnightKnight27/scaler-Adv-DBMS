#include <iostream>
#include <string>
#include <cassert>
#include "RedBlackTree.hpp"
#include "BTree.hpp"

void testRedBlackTree() {
    std::cout << "==================================================" << std::endl;
    std::cout << "          RED-BLACK TREE TEST SUITE" << std::endl;
    std::cout << "==================================================" << std::endl;

    RedBlackTree<int, std::string> rbt;
    std::string err;

    // Step 1: Step-by-step insertions
    std::cout << "--- Inserting Elements step-by-step ---" << std::endl;
    rbt.insert(15, "Fifteen");
    rbt.insert(10, "Ten");
    rbt.insert(20, "Twenty");
    rbt.insert(5, "Five");
    rbt.insert(30, "Thirty");
    rbt.insert(12, "Twelve");

    rbt.printTree();
    
    assert(rbt.verifyRBTProperties(err));
    std::cout << "[VERIFICATION] SUCCESS: All Red-Black Tree properties successfully verified." << std::endl;

    // Step 2: Search Operations
    std::cout << "\n--- Search Operations ---" << std::endl;
    std::string val;
    if (rbt.find(12, val)) std::cout << "Key 12 found! Value: " << val << std::endl;
    if (rbt.find(30, val)) std::cout << "Key 30 found! Value: " << val << std::endl;
    if (!rbt.find(99, val)) std::cout << "Key 99 NOT found!" << std::endl;

    // Step 3: Deletions
    std::cout << "\n--- Deleting Elements step-by-step ---" << std::endl;
    std::cout << "Deleting Key: 10" << std::endl;
    rbt.remove(10);
    rbt.printTree();
    assert(rbt.verifyRBTProperties(err));
    std::cout << "[VERIFICATION] SUCCESS: All Red-Black Tree properties successfully verified." << std::endl;

    std::cout << "Deleting Key: 15 (Root node)" << std::endl;
    rbt.remove(15);
    rbt.printTree();
    assert(rbt.verifyRBTProperties(err));
    std::cout << "[VERIFICATION] SUCCESS: All Red-Black Tree properties successfully verified." << std::endl;
}

void testBTree() {
    std::cout << "\n==================================================" << std::endl;
    std::cout << "                B-TREE TEST SUITE" << std::endl;
    std::cout << "==================================================" << std::endl;

    adbms::BTree<int, std::string> bt(2); // t=2, 2-3-4 tree
    std::string err;

    std::cout << "--- Inserting Elements step-by-step ---" << std::endl;
    bt.insert(10, "Ten");
    bt.insert(20, "Twenty");
    bt.insert(5, "Five");
    bt.insert(6, "Six");
    bt.insert(12, "Twelve");
    bt.insert(30, "Thirty");
    bt.insert(7, "Seven");

    bt.print();

    err = bt.verify();
    assert(err.empty());
    std::cout << "[VERIFICATION] SUCCESS: B-Tree invariants successfully verified." << std::endl;

    std::cout << "\n--- Search Operations ---" << std::endl;
    std::string val;
    if (bt.find(12, val)) std::cout << "Key 12 found! Value: " << val << std::endl;
    if (bt.find(30, val)) std::cout << "Key 30 found! Value: " << val << std::endl;
    if (!bt.find(99, val)) std::cout << "Key 99 NOT found!" << std::endl;

    std::cout << "\n--- Deleting Elements step-by-step ---" << std::endl;
    std::cout << "Deleting Key: 6" << std::endl;
    bt.erase(6);
    bt.print();
    err = bt.verify();
    assert(err.empty());
    std::cout << "[VERIFICATION] SUCCESS: B-Tree invariants successfully verified." << std::endl;

    std::cout << "Deleting Key: 20" << std::endl;
    bt.erase(20);
    bt.print();
    err = bt.verify();
    assert(err.empty());
    std::cout << "[VERIFICATION] SUCCESS: B-Tree invariants successfully verified." << std::endl;
}

int main() {
    testRedBlackTree();
    testBTree();
    std::cout << "\nALL LAB 4 TESTS COMPLETED SUCCESSFULLY!" << std::endl;
    return 0;
}
