// ============================================================
//  Lab Session 4 (Part 1): Red-Black Tree
//  Name      : Abdul Kalam Azad
//  Roll No.  : 24BCS10053
//  Build     : g++ -std=c++17 -o rbt RedBlackTree.cpp && ./rbt
// ============================================================

#include <iostream>
using namespace std;

enum Color {
    RED,
    BLACK
};

struct Node {
    int data;
    Color color;

    Node* left;
    Node* right;
    Node* parent;

    Node(int value) {
        data = value;
        color = RED;
        left = right = parent = nullptr;
    }
};

class RedBlackTree {
    private:
        Node* root;
        Node* NIL;

        // LEFT ROTATION
        void leftRotate(Node* x) {
            Node* y = x->right;

            x->right = y->left;
            if(y->left != NIL) y->left->parent = x;
            y->parent = x->parent;

            if(x->parent == nullptr) root = y;
            else if(x == x->parent->left) x->parent->left = y;
            else x->parent->right = y;

            y->left = x;
            x->parent = y;
        }

        // RIGHT ROTATION
        void rightRotate(Node* y) {
            Node* x = y->left;

            y->left = x->right;
            if(x->right != NIL) x->right->parent = y;
            x->parent = y->parent;

            if(y->parent == nullptr) root = x;
            else if(y == y->parent->left) y->parent->left = x;
            else y->parent->right = x;

            x->right = y;
            y->parent = x;
        }

        // INSERT FIX-UP
        void fixInsert(Node* node) {
            while(node->parent && node->parent->color == RED) {
                if(node->parent == node->parent->parent->left) {
                    Node* uncle = node->parent->parent->right;

                    // CASE 1: UNCLE IS RED
                    if(uncle->color == RED) {
                        node->parent->color = BLACK;
                        uncle->color = BLACK;
                        node->parent->parent->color = RED;

                        node = node->parent->parent;
                    }
                    else {
                        // CASE 2: TRIANGLE
                        if(node == node->parent->right) {
                            node = node->parent;
                            leftRotate(node);
                        }

                        // CASE 3: LINE
                        node->parent->color = BLACK;
                        node->parent->parent->color = RED;

                        rightRotate(node->parent->parent);
                    }
                }
                else {
                    Node* uncle = node->parent->parent->left;

                    if(uncle->color == RED) {
                        node->parent->color = BLACK;
                        uncle->color = BLACK;
                        node->parent->parent->color = RED;

                        node = node->parent->parent;
                    }
                    else {
                        if(node == node->parent->left) {
                            node = node->parent;
                            rightRotate(node);
                        }

                        node->parent->color = BLACK;
                        node->parent->parent->color = RED;

                        leftRotate(node->parent->parent);
                    }
                }
            }

            root->color = BLACK;
        }

        // TRANSPLANT SUBTREE
        void transplant(Node* u, Node* v) {
            if(u->parent == nullptr) root = v;
            else if(u == u->parent->left) u->parent->left = v;
            else u->parent->right = v;

            v->parent = u->parent;
        }

        // FIND MINIMUM NODE
        Node* minimum(Node* node) {
            while(node->left != NIL) node = node->left;
            return node;
        }

        // DELETE FIX-UP
        void fixDelete(Node* x) {
            while(x != root && x->color == BLACK) {
                if(x == x->parent->left) {
                    Node* sibling = x->parent->right;

                    // CASE 1: SIBLING IS RED
                    if(sibling->color == RED) {
                        sibling->color = BLACK;
                        x->parent->color = RED;

                        leftRotate(x->parent);

                        sibling = x->parent->right;
                    }

                    // CASE 2: BOTH CHILDREN BLACK
                    if(sibling->left->color == BLACK && sibling->right->color == BLACK) {
                        sibling->color = RED;
                        x = x->parent;
                    }
                    else {
                        // CASE 3: FAR CHILD BLACK
                        if(sibling->right->color == BLACK) {
                            sibling->left->color = BLACK;
                            sibling->color = RED;

                            rightRotate(sibling);

                            sibling = x->parent->right;
                        }

                        // CASE 4: FAR CHILD RED
                        sibling->color = x->parent->color;
                        x->parent->color = BLACK;
                        sibling->right->color = BLACK;

                        leftRotate(x->parent);

                        x = root;
                    }
                }
                else {
                    Node* sibling = x->parent->left;

                    if(sibling->color == RED) {
                        sibling->color = BLACK;
                        x->parent->color = RED;

                        rightRotate(x->parent);

                        sibling = x->parent->left;
                    }

                    if(sibling->left->color == BLACK && sibling->right->color == BLACK) {
                        sibling->color = RED;
                        x = x->parent;
                    }
                    else {
                        if(sibling->left->color == BLACK) {
                            sibling->right->color = BLACK;
                            sibling->color = RED;

                            leftRotate(sibling);

                            sibling = x->parent->left;
                        }

                        sibling->color = x->parent->color;
                        x->parent->color = BLACK;
                        sibling->left->color = BLACK;

                        rightRotate(x->parent);

                        x = root;
                    }
                }
            }

            x->color = BLACK;
        }

        // SEARCH HELPER
        Node* searchHelper(Node* node, int key) {
            if(node == NIL || key == node->data) return node;
            if(key < node->data) return searchHelper(node->left, key);
            return searchHelper(node->right, key);
        }

        // INORDER HELPER
        void inorderHelper(Node* node) {
            if(node == NIL) return;

            inorderHelper(node->left);

            cout << node->data
                << "("
                << (node->color == RED ? "R" : "B")
                << ") ";

            inorderHelper(node->right);
        }

        // MEMORY CLEANUP
        void destroy(Node* node) {
            if(node == NIL) return;

            destroy(node->left);
            destroy(node->right);

            delete node;
        }

    public:
        // CONSTRUCTOR
        RedBlackTree() {
            NIL = new Node(0);
            NIL->color = BLACK;

            NIL->left = NIL;
            NIL->right = NIL;

            root = NIL;
        }

        // DESTRUCTOR
        ~RedBlackTree() {
            destroy(root);
            delete NIL;
        }

        // INSERT NODE
        void insert(int key) {
            Node* node = new Node(key);

            node->left = NIL;
            node->right = NIL;

            Node* parent = nullptr;
            Node* current = root;

            while(current != NIL) {
                parent = current;

                if(key < current->data) current = current->left;
                else current = current->right;
            }

            node->parent = parent;

            if(parent == nullptr) root = node;
            else if(key < parent->data) parent->left = node;
            else parent->right = node;

            if(node->parent == nullptr) {
                node->color = BLACK;
                return;
            }

            if(node->parent->parent == nullptr) return;

            fixInsert(node);
        }

        // DELETE NODE
        void deleteNode(int key) {
            Node* z = searchHelper(root, key);

            if(z == NIL) {
                cout << "Key not found\n";
                return;
            }

            Node* y = z;
            Color originalColor = y->color;
            Node* x;

            if(z->left == NIL) {
                x = z->right;
                transplant(z, z->right);
            }
            else if(z->right == NIL) {
                x = z->left;
                transplant(z, z->left);
            }
            else {
                y = minimum(z->right);
                originalColor = y->color;

                x = y->right;

                if(y->parent == z) {
                    x->parent = y;
                }
                else {
                    transplant(y, y->right);

                    y->right = z->right;
                    y->right->parent = y;
                }

                transplant(z, y);

                y->left = z->left;
                y->left->parent = y;

                y->color = z->color;
            }

            delete z;

            if(originalColor == BLACK) fixDelete(x);
        }

        // SEARCH
        bool search(int key) {
            return searchHelper(root, key) != NIL;
        }

        // INORDER TRAVERSAL
        void inorder() {
            inorderHelper(root);
            cout << endl;
        }
};

int main() {
    RedBlackTree tree;

    tree.insert(10);
    tree.insert(20);
    tree.insert(30);
    tree.insert(15);
    tree.insert(5);
    tree.insert(25);

    cout << "Inorder Traversal:\n";
    tree.inorder();

    cout << "\nSearching 15: ";

    if(tree.search(15)) cout << "Found\n";
    else cout << "Not Found\n";

    cout << "\nDeleting 20...\n";
    tree.deleteNode(20);

    cout << "\nInorder Traversal After Deletion:\n";
    tree.inorder();

    return 0;
}