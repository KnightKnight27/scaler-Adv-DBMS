#include "../include/RedBlackTree.h"

int main() {

    RedBlackTree tree;

    tree.insert(10);
    tree.insert(20);
    tree.insert(30);
    tree.insert(15);
    tree.insert(25);
    tree.insert(5);

    cout << "Inorder Traversal:" << endl;

    tree.inorder();

    return 0;
}