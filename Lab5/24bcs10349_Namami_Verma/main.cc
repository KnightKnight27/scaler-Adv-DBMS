#include "RedBlackTree.h"

int main() {
    RedBlackTree tree;

    tree.insert(10);
    tree.insert(20);
    tree.insert(30);
    tree.insert(15);
    tree.insert(25);

    cout << "Inorder Traversal: ";
    tree.inorder();

    return 0;
}