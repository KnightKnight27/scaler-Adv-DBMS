#include <iostream>
using namespace std;

enum Color { RED, BLACK };

struct Node {
    int data;
    Color color;
    Node *left, *right, *parent;

    Node(int data) {
        this->data = data;
        left = right = parent = nullptr;
        this->color = RED;
    }
};

class RedBlackTree {
private:
    Node *root;
protected:
    void rotateLeft(Node *&, Node *&);
    void rotateRight(Node *&, Node *&);
    void fixInsert(Node *&, Node *&);
public:
    RedBlackTree() { root = nullptr; }
    void insert(const int &n);
    void inorder();
    void inorderHelper(Node *root);
};

void RedBlackTree::inorderHelper(Node *root) {
    if (root == nullptr)
        return;
    inorderHelper(root->left);
    cout << root->data << " ";
    inorderHelper(root->right);
}

void RedBlackTree::inorder() {
    inorderHelper(root);
    cout << endl;
}

void RedBlackTree::rotateLeft(Node *&root, Node *&pt) {
    Node *pt_right = pt->right;
    pt->right = pt_right->left;
    if (pt->right != nullptr)
        pt->right->parent = pt;
    pt_right->parent = pt->parent;
    if (pt->parent == nullptr)
        root = pt_right;
    else if (pt == pt->parent->left)
        pt->parent->left = pt_right;
    else
        pt->parent->right = pt_right;
    pt_right->left = pt;
    pt->parent = pt_right;
}

void RedBlackTree::rotateRight(Node *&root, Node *&pt) {
    Node *pt_left = pt->left;
    pt->left = pt_left->right;
    if (pt->left != nullptr)
        pt->left->parent = pt;
    pt_left->parent = pt->parent;
    if (pt->parent == nullptr)
        root = pt_left;
    else if (pt == pt->parent->left)
        pt->parent->left = pt_left;
    else
        pt->parent->right = pt_left;
    pt_left->right = pt;
    pt->parent = pt_left;
}

void RedBlackTree::fixInsert(Node *&root, Node *&pt) {
    Node *parent_pt = nullptr;
    Node *grand_parent_pt = nullptr;
    while ((pt != root) && (pt->color != BLACK) && (pt->parent->color == RED)) {
        parent_pt = pt->parent;
        grand_parent_pt = pt->parent->parent;
        if (parent_pt == grand_parent_pt->left) {
            Node *uncle_pt = grand_parent_pt->right;
            if (uncle_pt != nullptr && uncle_pt->color == RED) {
                grand_parent_pt->color = RED;
                parent_pt->color = BLACK;
                uncle_pt->color = BLACK;
                pt = grand_parent_pt;
            } else {
                if (pt == parent_pt->right) {
                    rotateLeft(root, parent_pt);
                    pt = parent_pt;
                    parent_pt = pt->parent;
                }
                rotateRight(root, grand_parent_pt);
                swap(parent_pt->color, grand_parent_pt->color);
                pt = parent_pt;
            }
        } else {
            Node *uncle_pt = grand_parent_pt->left;
            if ((uncle_pt != nullptr) && (uncle_pt->color == RED)) {
                grand_parent_pt->color = RED;
                parent_pt->color = BLACK;
                uncle_pt->color = BLACK;
                pt = grand_parent_pt;
            } else {
                if (pt == parent_pt->left) {
                    rotateRight(root, parent_pt);
                    pt = parent_pt;
                    parent_pt = pt->parent;
                }
                rotateLeft(root, grand_parent_pt);
                swap(parent_pt->color, grand_parent_pt->color);
                pt = parent_pt;
            }
        }
    }
    root->color = BLACK;
}

void RedBlackTree::insert(const int &data) {
    Node *pt = new Node(data);
    root = (root == nullptr) ? pt : root;
    if (root == pt) {
        pt->color = BLACK;
        return;
    }
    Node *curr = root, *parent = nullptr;
    while (curr != nullptr) {
        parent = curr;
        if (pt->data < curr->data)
            curr = curr->left;
        else if (pt->data > curr->data)
            curr = curr->right;
        else
            return; // Duplicate data not allowed
    }
    pt->parent = parent;
    if (pt->data < parent->data)
        parent->left = pt;
    else
        parent->right = pt;
    fixInsert(root, pt);
}

int main() {
    RedBlackTree rbt;
    int arr[] = {7, 3, 18, 10, 22, 8, 11, 26, 2, 6, 13};
    int n = sizeof(arr)/sizeof(arr[0]);
    for (int i = 0; i < n; i++) {
        rbt.insert(arr[i]);
    }
    cout << "Inorder Traversal of Created Red-Black Tree:" << endl;
    rbt.inorder();
    return 0;
}
