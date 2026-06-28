// Combined build for Lab 4: compile both RBT and B-Tree
// Usage: g++ -std=c++17 -DWHICH=rbt rbt.cpp -o rbt && g++ -std=c++17 -DWHICH=btree btree.cpp -o btree
// Or simply: g++ -std=c++17 -o rbt rbt.cpp && g++ -std=c++17 -o btree btree.cpp

#include <iostream>

int main() {
    std::cout << "Compile separately: g++ -std=c++17 -o rbt rbt.cpp && ./rbt\n";
    std::cout << "                     g++ -std=c++17 -o btree btree.cpp && ./btree\n";
    return 0;
}
