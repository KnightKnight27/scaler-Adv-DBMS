#include "BTree.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <random>
#include <cassert>
#include <stdexcept>

template <typename K, typename V>
void verifyTree(const adbms::BTree<K, V>& tree, const std::string& action) {
    std::string err = tree.verify();
    if (!err.empty()) {
        std::cerr << "\033[1;31m[VERIFICATION FAILED]\033[0m Action: " << action << " | Error: " << err << "\n";
        throw std::runtime_error("B-Tree Invariant Violation: " + err);
    }
}

void printHeading(const std::string& heading) {
    std::cout << "\n\033[1;36m==================================================\n";
    std::cout << "  " << heading << "\n";
    std::cout << "==================================================\033[0m\n";
}

int main() {
    printHeading("TEST 1: SMALL B-TREE (t=3) INSERTIONS");

    adbms::BTree<int, std::string> smallTree(3);
    std::vector<std::pair<int, std::string>> initialInsertions = {
        {41, "Inception"}, {17, "Whiplash"},     {76, "Parasite"},
        { 9, "Memento"},   {23, "Dune"},         {58, "Oppenheimer"},
        {88, "Joker"},     { 3, "La La Land"},   {12, "Spirited Away"},
        {30, "Goodfellas"}
    };

    for (const auto& pair : initialInsertions) {
        std::cout << "Inserting Key: " << pair.first << " -> " << pair.second << "\n";
        smallTree.insert(pair.first, pair.second);
        verifyTree(smallTree, "insert(" + std::to_string(pair.first) + ")");
    }

    std::cout << "\nTree Size: " << smallTree.size() << "\n";
    std::cout << "Tree Structure:\n";
    smallTree.print();

    std::cout << "\nIn-Order Traversal:\n";
    smallTree.inOrder([](int k, const std::string& v) {
        std::cout << "  " << k << " : " << v << "\n";
    });

    printHeading("TEST 2: SEARCH & OVERWRITE OPERATIONS");
    
    std::vector<int> lookupKeys = {17, 23, 99};
    for (int k : lookupKeys) {
        std::cout << "Contains(" << k << ") -> " << (smallTree.contains(k) ? "\033[1;32mYES\033[0m" : "\033[1;31mNO\033[0m") << "\n";
    }

    std::cout << "\nOverwriting Key 58...\n";
    smallTree.insert(58, "Oppenheimer (IMAX Edition)");
    verifyTree(smallTree, "overwrite(58)");
    std::cout << "Value at 58: " << smallTree.at(58) << " | Size: " << smallTree.size() << "\n";

    try {
        smallTree.at(999);
    } catch (const std::out_of_range& e) {
        std::cout << "Lookup of non-existent key 999 threw as expected: " << e.what() << "\n";
    }

    printHeading("TEST 3: ERASURE & ROOT COLLAPSE");

    std::vector<int> eraseKeys = {3, 17, 41, 88, 23};
    for (int k : eraseKeys) {
        std::cout << "Erasing Key: " << k << " ... ";
        bool ok = smallTree.erase(k);
        verifyTree(smallTree, "erase(" + std::to_string(k) + ")");
        std::cout << (ok ? "\033[1;32mSUCCESS\033[0m" : "\033[1;31mMISS\033[0m") << " | Current Size: " << smallTree.size() << "\n";
    }

    std::cout << "\nTree Structure After Erasures:\n";
    smallTree.print();

    printHeading("TEST 4: SEQUENTIAL INSERTION (t=2, 2-3-4 tree)");
    adbms::BTree<int, int> twothreefour(2);
    for (int i = 1; i <= 16; ++i) {
        twothreefour.insert(i, i * i);
        verifyTree(twothreefour, "sequential insert(" + std::to_string(i) + ")");
    }
    twothreefour.print();

    printHeading("TEST 5: LARGE-SCALE STRESS TEST AGAINST STL MAP ORACLE");
    
    std::cout << "Running 5,000 random operations (insert/erase) on degree t=4 B-Tree...\n";
    adbms::BTree<int, int> stressTree(4);
    std::map<int, int> oracleMap;

    std::mt19937 rng(42); // Seed for reproducibility
    std::uniform_int_distribution<int> keyDist(0, 399);
    std::uniform_int_distribution<int> opDist(0, 1);

    for (int step = 1; step <= 5000; ++step) {
        int k = keyDist(rng);
        if (opDist(rng) == 0) {
            // Insert
            stressTree.insert(k, step);
            oracleMap[k] = step;
        } else {
            // Erase
            stressTree.erase(k);
            oracleMap.erase(k);
        }

        if (step % 500 == 0) {
            verifyTree(stressTree, "stress step " + std::to_string(step));
            assert(stressTree.size() == oracleMap.size());
        }
    }

    verifyTree(stressTree, "stress final");
    std::cout << "Stress Test Complete. Tree size: " << stressTree.size() << " elements.\n";

    // Double check key sequences match exactly
    std::vector<int> btreeKeys;
    stressTree.inOrder([&btreeKeys](int k, int) {
        btreeKeys.push_back(k);
    });

    std::vector<int> mapKeys;
    for (const auto& pair : oracleMap) {
        mapKeys.push_back(pair.first);
    }

    assert(btreeKeys == mapKeys);
    std::cout << "\033[1;32m[VERIFICATION SUCCESS]\033[0m In-order traversals of B-Tree and std::map match exactly.\n";

    std::cout << "\n\033[1;32m==================================================\n";
    std::cout << "  ALL B-TREE IMPLEMENTATION TESTS PASSED!\n";
    std::cout << "==================================================\033[0m\n";

    return 0;
}
