#include "RedBlackTree.hpp"
#include <iostream>
#include <cassert>

void printSeparator() {
    std::cout << std::string(50, '=') << std::endl;
}

void testInsertions() {
    printSeparator();
    std::cout << "TEST 1: Insertions and Basic Properties" << std::endl;
    printSeparator();

    RedBlackTree<int> tree;

    std::cout << "\nInserting: 10, 20, 30, 15, 25, 5, 2" << std::endl;
    tree.insert(10);
    tree.insert(20);
    tree.insert(30);
    tree.insert(15);
    tree.insert(25);
    tree.insert(5);
    tree.insert(2);

    tree.printTree();
    std::cout << "\n";
    tree.inorder();
    tree.preorder();
    tree.postorder();

    std::cout << "\nValid RB Tree: " << (tree.isValid() ? "YES" : "NO") << std::endl;
    std::cout << "Black Height: " << tree.getBlackHeight() << std::endl;
}

void testSearch() {
    printSeparator();
    std::cout << "TEST 2: Search Operations" << std::endl;
    printSeparator();

    RedBlackTree<int> tree;
    int values[] = {50, 30, 70, 20, 40, 60, 80, 10, 25, 35, 45};
    for (int v : values) tree.insert(v);

    std::cout << "\nSearching for existing values:" << std::endl;
    std::cout << "Search 30: " << (tree.search(30) ? "FOUND" : "NOT FOUND") << std::endl;
    std::cout << "Search 70: " << (tree.search(70) ? "FOUND" : "NOT FOUND") << std::endl;
    std::cout << "Search 10: " << (tree.search(10) ? "FOUND" : "NOT FOUND") << std::endl;

    std::cout << "\nSearching for non-existing values:" << std::endl;
    std::cout << "Search 99: " << (tree.search(99) ? "FOUND" : "NOT FOUND") << std::endl;
    std::cout << "Search 5:  " << (tree.search(5) ? "FOUND" : "NOT FOUND") << std::endl;
}

void testDeletions() {
    printSeparator();
    std::cout << "TEST 3: Deletions (All Cases)" << std::endl;
    printSeparator();

    RedBlackTree<int> tree;
    int values[] = {50, 30, 70, 20, 40, 60, 80, 10, 25, 35, 45, 55, 65, 75, 85};
    for (int v : values) tree.insert(v);

    std::cout << "\nInitial tree:" << std::endl;
    tree.inorder();
    std::cout << "Valid: " << (tree.isValid() ? "YES" : "NO") << std::endl;

    std::cout << "\n--- Deleting red node (35) ---" << std::endl;
    tree.remove(35);
    tree.inorder();
    std::cout << "Valid: " << (tree.isValid() ? "YES" : "NO") << std::endl;

    std::cout << "\n--- Deleting node with one child (25) ---" << std::endl;
    tree.remove(25);
    tree.inorder();
    std::cout << "Valid: " << (tree.isValid() ? "YES" : "NO") << std::endl;

    std::cout << "\n--- Deleting node with two children (50 - root) ---" << std::endl;
    tree.remove(50);
    tree.inorder();
    std::cout << "Valid: " << (tree.isValid() ? "YES" : "NO") << std::endl;

    std::cout << "\n--- Deleting non-existent value (99) ---" << std::endl;
    tree.remove(99);

    std::cout << "\n--- Deleting multiple nodes ---" << std::endl;
    int toDelete[] = {20, 30, 40, 60, 70, 80};
    for (int v : toDelete) {
        tree.remove(v);
        std::cout << "After deleting " << v << ": ";
        tree.inorder();
        std::cout << "Valid: " << (tree.isValid() ? "YES" : "NO") << std::endl;
    }
}

void testEmptyTree() {
    printSeparator();
    std::cout << "TEST 4: Empty Tree Edge Cases" << std::endl;
    printSeparator();

    RedBlackTree<int> tree;
    std::cout << "Is empty: " << (tree.isEmpty() ? "YES" : "NO") << std::endl;
    std::cout << "Valid: " << (tree.isValid() ? "YES" : "NO") << std::endl;
    std::cout << "Search 10: " << (tree.search(10) ? "FOUND" : "NOT FOUND") << std::endl;
    std::cout << "Remove 10: ";
    tree.remove(10);
}

void testLargeInsertions() {
    printSeparator();
    std::cout << "TEST 5: Large Scale Insertion (100 elements)" << std::endl;
    printSeparator();

    RedBlackTree<int> tree;
    std::cout << "\nInserting values 1-100..." << std::endl;
    for (int i = 1; i <= 100; i++) tree.insert(i);

    std::cout << "Valid RB Tree: " << (tree.isValid() ? "YES" : "NO") << std::endl;
    std::cout << "Black Height: " << tree.getBlackHeight() << std::endl;
    std::cout << "Search 1:   " << (tree.search(1) ? "FOUND" : "NOT FOUND") << std::endl;
    std::cout << "Search 50:  " << (tree.search(50) ? "FOUND" : "NOT FOUND") << std::endl;
    std::cout << "Search 100: " << (tree.search(100) ? "FOUND" : "NOT FOUND") << std::endl;
    std::cout << "Search 101: " << (tree.search(101) ? "FOUND" : "NOT FOUND") << std::endl;

    std::cout << "\nDeleting all even numbers..." << std::endl;
    for (int i = 2; i <= 100; i += 2) tree.remove(i);
    std::cout << "Valid after deletions: " << (tree.isValid() ? "YES" : "NO") << std::endl;
    std::cout << "Black Height: " << tree.getBlackHeight() << std::endl;
}

void testStringTree() {
    printSeparator();
    std::cout << "TEST 6: String Type Red-Black Tree" << std::endl;
    printSeparator();

    RedBlackTree<std::string> tree;
    tree.insert("banana");
    tree.insert("apple");
    tree.insert("cherry");
    tree.insert("date");
    tree.insert("elderberry");

    std::cout << "\nInorder: ";
    tree.inorder();
    std::cout << "Valid: " << (tree.isValid() ? "YES" : "NO") << std::endl;
    std::cout << "Search 'cherry': " << (tree.search("cherry") ? "FOUND" : "NOT FOUND") << std::endl;

    tree.remove("banana");
    std::cout << "\nAfter deleting 'banana': ";
    tree.inorder();
    std::cout << "Valid: " << (tree.isValid() ? "YES" : "NO") << std::endl;
}

int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║         RED-BLACK TREE IMPLEMENTATION                   ║" << std::endl;
    std::cout << "║         24BCS10285 - Vedanshu Nishad                    ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n";

    testInsertions();
    testSearch();
    testDeletions();
    testEmptyTree();
    testLargeInsertions();
    testStringTree();

    printSeparator();
    std::cout << "ALL TESTS COMPLETED SUCCESSFULLY!" << std::endl;
    printSeparator();

    return 0;
}
