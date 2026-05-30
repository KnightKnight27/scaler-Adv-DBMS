#include <iostream>
#include <string>
#include "RedBlackTree.h"

struct Employee {
    int         id;
    std::string name;
    bool operator<(const Employee& o) const { return id < o.id; }
};

int main() {
    {
        std::cout << "=== int tree ===\n";
        RedBlackTree<int> tree;
        for (int v : {10, 20, 5, 15, 1, 7, 25})
            tree.insert(v);
        tree.inorder([](const int& v){ std::cout << v << " "; });
        std::cout << "\n";

        tree.remove(10);
        tree.remove(1);
        tree.inorder([](const int& v){ std::cout << v << " "; });
        std::cout << "\n";

        std::cout << "contains 7:  " << tree.contains(7)  << "\n";
        std::cout << "contains 10: " << tree.contains(10) << "\n";
    }

    {
        std::cout << "\n=== string tree ===\n";
        RedBlackTree<std::string> tree;
        for (const auto& s : {"banana", "apple", "cherry", "date", "avocado"})
            tree.insert(s);
        tree.inorder([](const std::string& s){ std::cout << s << " "; });
        std::cout << "\n";
    }

    {
        std::cout << "\n=== Employee tree (sorted by id) ===\n";
        RedBlackTree<Employee> tree;
        tree.insert({3, "Charlie"});
        tree.insert({1, "Alice"});
        tree.insert({2, "Bob"});
        tree.insert({5, "Eve"});
        tree.insert({4, "Dave"});
        tree.inorder([](const Employee& e){
            std::cout << "[" << e.id << "] " << e.name << "\n";
        });
    }

    return 0;
}