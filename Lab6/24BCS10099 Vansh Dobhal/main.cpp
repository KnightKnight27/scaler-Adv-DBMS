// ADBMS Lab 6 - B-Tree Demo
// Name: Vansh Dobhal
// Roll No: 24BCS10099

#include "b_tree.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <random>

using namespace lab6;

template <typename K, typename V>
void check_tree(const BTree<K, V>& tree, const std::string& context) {
    std::string error = tree.validate();
    if (!error.empty()) {
        std::cerr << "VALIDATION FAILED [" << context << "]: " << error << "\n";
        std::exit(EXIT_FAILURE);
    }
}

void print_header(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

int main() {
    print_header("1) Basic Operations (t=3) - Inserting Books");
    BTree<int, std::string> btree(3);
    
    std::vector<std::pair<int, std::string>> books = {
        {101, "The Great Gatsby"}, {55, "1984"}, {89, "To Kill a Mockingbird"},
        {12, "Pride and Prejudice"}, {34, "The Catcher in the Rye"}, {77, "Moby Dick"},
        {99, "War and Peace"}, {2, "Hamlet"}, {44, "The Odyssey"}, {66, "Ulysses"}
    };

    for (const auto& [k, v] : books) {
        btree.insert(k, v);
        check_tree(btree, "After inserting " + std::to_string(k));
    }

    std::cout << "Tree size: " << btree.size() << "\n";
    btree.display();
    std::cout << "In-order traversal:\n";
    btree.iterate([](int k, const std::string& v) {
        std::cout << k << ": " << v << " | ";
    });
    std::cout << "\n";

    print_header("2) Lookup and Update");
    for (int k : {55, 99, 100}) {
        std::cout << "Contains " << k << "? " << (btree.contains(k) ? "Yes" : "No") << "\n";
    }

    std::cout << "Get 89: " << btree.get(89) << "\n";
    btree.insert(89, "To Kill a Mockingbird (Special Edition)"); // Update
    std::cout << "Updated 89: " << btree.get(89) << " (Size: " << btree.size() << ")\n";

    try {
        btree.get(999);
    } catch (const std::out_of_range& e) {
        std::cout << "Exception caught for missing key: " << e.what() << "\n";
    }

    print_header("3) Deletions");
    for (int k : {12, 55, 101, 2, 77}) {
        bool removed = btree.remove(k);
        check_tree(btree, "After deleting " + std::to_string(k));
        std::cout << "Removed " << k << "? " << (removed ? "Yes" : "No") << ", Size: " << btree.size() << "\n";
    }
    
    std::cout << "Tree after deletions:\n";
    btree.display();

    print_header("4) Sequential Inserts (t=2, Worst Case)");
    BTree<int, int> seq_tree(2);
    for (int i = 1; i <= 20; ++i) {
        seq_tree.insert(i, i * 10);
        check_tree(seq_tree, "Sequential insert " + std::to_string(i));
    }
    seq_tree.display();

    print_header("5) Large Tree (t=40) - 40,000 Inserts");
    BTree<int, int> large_tree(40);
    std::mt19937 gen(42); // fixed seed
    std::vector<int> random_keys;
    
    for (int i = 0; i < 40000; ++i) {
        int key = gen() % 1000000;
        large_tree.insert(key, key);
        random_keys.push_back(key);
    }
    
    check_tree(large_tree, "40k random inserts");
    int found_count = 0;
    for (int i = 0; i < 1000; ++i) {
        if (large_tree.contains(random_keys[i])) found_count++;
    }
    std::cout << "Found " << found_count << "/1000 keys. Total size: " << large_tree.size() << "\n";

    print_header("6) Stress Test vs std::map");
    BTree<int, int> stress_tree(5);
    std::map<int, int> reference_map;
    std::uniform_int_distribution<int> dist(1, 500);

    for (int i = 0; i < 8000; ++i) {
        int key = dist(gen);
        if (gen() % 2 == 0) { // Insert
            stress_tree.insert(key, i);
            reference_map[key] = i;
        } else { // Delete
            stress_tree.remove(key);
            reference_map.erase(key);
        }

        if (i % 500 == 0) check_tree(stress_tree, "Stress iteration " + std::to_string(i));
    }

    check_tree(stress_tree, "Stress Final");
    
    if (stress_tree.size() != reference_map.size()) {
        std::cerr << "Size mismatch!\n";
        return 1;
    }

    std::vector<int> tree_keys, map_keys;
    stress_tree.iterate([&](int k, int) { tree_keys.push_back(k); });
    for (auto const& [k, v] : reference_map) map_keys.push_back(k);

    if (tree_keys != map_keys) {
        std::cerr << "Order mismatch!\n";
        return 1;
    }

    std::cout << "Stress test passed! " << stress_tree.size() << " elements validated against std::map.\n";
    std::cout << "All B-Tree validations successful.\n";

    return 0;
}
