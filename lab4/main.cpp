/**
 * Lab 4 — Red-Black Tree + B-Tree: Driver Program
 *
 * Tests both tree implementations with insert, delete, search,
 * and property verification.
 */

#include "rbtree.h"
#include "btree.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <cassert>

// ─────────────────────────────────────────────────
// Test Red-Black Tree
// ─────────────────────────────────────────────────
void test_rbtree() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Red-Black Tree Tests                                       ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    RedBlackTree<int> rbt;

    // Test 1: Sequential insertions
    std::cout << "\n--- Test 1: Sequential Insertions ---" << std::endl;
    std::vector<int> values = {10, 20, 30, 15, 25, 5, 1, 8, 12, 18, 22, 28, 35, 40, 3};
    for (int v : values) {
        rbt.insert(v);
        std::cout << "  Inserted " << v << " → valid: " << (rbt.verify() ? "YES ✓" : "NO ✗") << std::endl;
    }

    std::cout << "\n  Tree structure:" << std::endl;
    rbt.print();

    std::cout << "\n  Inorder traversal: ";
    rbt.inorder([](int key, Color color) {
        std::cout << key << "(" << (color == Color::RED ? "R" : "B") << ") ";
    });
    std::cout << std::endl;
    std::cout << "  Size: " << rbt.size() << std::endl;

    // Test 2: Search
    std::cout << "\n--- Test 2: Search ---" << std::endl;
    for (int v : {5, 15, 25, 42, 0}) {
        std::cout << "  Search " << v << ": " << (rbt.search(v) ? "FOUND ✓" : "NOT FOUND") << std::endl;
    }

    // Test 3: Deletions
    std::cout << "\n--- Test 3: Deletions ---" << std::endl;
    std::vector<int> to_delete = {1, 10, 20, 30, 15};
    for (int v : to_delete) {
        bool ok = rbt.remove(v);
        bool valid = rbt.verify();
        std::cout << "  Deleted " << v << " → removed: " << (ok ? "YES" : "NO")
                  << ", valid: " << (valid ? "YES ✓" : "NO ✗")
                  << ", size: " << rbt.size() << std::endl;
    }

    std::cout << "\n  Tree after deletions:" << std::endl;
    rbt.print();

    // Test 4: Stress test
    std::cout << "\n--- Test 4: Stress Test (1000 random insert/delete) ---" << std::endl;
    RedBlackTree<int> stress_tree;
    std::mt19937 rng(42);
    std::vector<int> inserted;

    for (int i = 0; i < 500; i++) {
        int v = rng() % 10000;
        stress_tree.insert(v);
        inserted.push_back(v);
    }
    std::cout << "  After 500 insertions: size=" << stress_tree.size()
              << ", valid=" << (stress_tree.verify() ? "YES ✓" : "NO ✗") << std::endl;

    std::shuffle(inserted.begin(), inserted.end(), rng);
    for (int i = 0; i < 250; i++) {
        stress_tree.remove(inserted[i]);
    }
    std::cout << "  After 250 deletions:  size=" << stress_tree.size()
              << ", valid=" << (stress_tree.verify() ? "YES ✓" : "NO ✗") << std::endl;
}

// ─────────────────────────────────────────────────
// Test B-Tree
// ─────────────────────────────────────────────────
void test_btree() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  B-Tree Tests (min_degree t=3, max keys=5 per node)         ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    BTree<int> bt(3);  // min degree 3 → max 5 keys per node

    // Test 1: Insertions with splits
    std::cout << "\n--- Test 1: Insertions (observe splits) ---" << std::endl;
    std::vector<int> values = {10, 20, 5, 6, 12, 30, 7, 17, 3, 4, 25, 35, 40, 8, 15, 1, 2, 50, 45, 22};

    for (int v : values) {
        bt.insert(v);
        std::cout << "  Inserted " << std::setw(2) << v
                  << " → valid: " << (bt.verify() ? "YES ✓" : "NO ✗") << std::endl;
    }

    std::cout << "\n  B-Tree structure:" << std::endl;
    bt.print();

    std::cout << "\n  Inorder traversal: ";
    bt.inorder([](const int& key) { std::cout << key << " "; });
    std::cout << std::endl;
    std::cout << "  Size: " << bt.size() << std::endl;

    // Test 2: Search
    std::cout << "\n--- Test 2: Search ---" << std::endl;
    for (int v : {5, 20, 40, 100, 22}) {
        std::cout << "  Search " << v << ": " << (bt.search(v) ? "FOUND ✓" : "NOT FOUND") << std::endl;
    }

    // Test 3: Deletions (borrow & merge)
    std::cout << "\n--- Test 3: Deletions (borrow from sibling / merge) ---" << std::endl;
    std::vector<int> to_delete = {3, 7, 10, 20, 50, 45, 1, 2, 40, 35};
    for (int v : to_delete) {
        std::cout << "\n  Deleting " << v << "..." << std::endl;
        bool ok = bt.remove(v);
        bool valid = bt.verify();
        std::cout << "  → removed: " << (ok ? "YES" : "NO")
                  << ", valid: " << (valid ? "YES ✓" : "NO ✗")
                  << ", size: " << bt.size() << std::endl;
        std::cout << "  Tree: ";
        bt.print();
    }

    // Test 4: Different minimum degrees
    std::cout << "\n--- Test 4: B-Tree with t=2 (2-3-4 tree) ---" << std::endl;
    BTree<int> bt2(2);  // 2-3-4 tree
    for (int i = 1; i <= 20; i++) bt2.insert(i);
    std::cout << "  Tree structure:" << std::endl;
    bt2.print();
    std::cout << "  Valid: " << (bt2.verify() ? "YES ✓" : "NO ✗") << std::endl;

    // Delete all
    std::cout << "\n  Deleting all keys 1..20:" << std::endl;
    for (int i = 1; i <= 20; i++) {
        bt2.remove(i);
        std::cout << "    Deleted " << std::setw(2) << i
                  << " → valid: " << (bt2.verify() ? "YES ✓" : "NO ✗")
                  << ", size: " << bt2.size() << std::endl;
    }

    // Test 5: Stress test
    std::cout << "\n--- Test 5: Stress Test (1000 random insert/delete) ---" << std::endl;
    BTree<int> stress_bt(4);  // min degree 4
    std::mt19937 rng(42);
    std::vector<int> inserted;

    for (int i = 0; i < 500; i++) {
        int v = rng() % 10000;
        if (!stress_bt.search(v)) {
            stress_bt.insert(v);
            inserted.push_back(v);
        }
    }
    std::cout << "  After insertions: size=" << stress_bt.size()
              << ", valid=" << (stress_bt.verify() ? "YES ✓" : "NO ✗") << std::endl;

    std::shuffle(inserted.begin(), inserted.end(), rng);
    int delete_count = std::min(250, static_cast<int>(inserted.size()));
    for (int i = 0; i < delete_count; i++) {
        stress_bt.remove(inserted[i]);
    }
    std::cout << "  After deletions:  size=" << stress_bt.size()
              << ", valid=" << (stress_bt.verify() ? "YES ✓" : "NO ✗") << std::endl;
}

// ─────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────
int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Lab 4: Red-Black Tree + B-Tree                            ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    test_rbtree();
    test_btree();

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Lab 4 Complete!                                            ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    return 0;
}
