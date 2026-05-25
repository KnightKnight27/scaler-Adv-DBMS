#include "RedBlackTree.h"

int main() {
    RedBlackTree tree;

    int values[] = {10, 20, 30, 15, 25, 5, 1};
    int n = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < n; i++) {
        tree.insert(values[i]);
    }

    cout << "Inorder Traversal:" << endl;
    tree.inorder();

    cout << "Preorder Traversal:" << endl;
    tree.preorder();

    return 0;
}