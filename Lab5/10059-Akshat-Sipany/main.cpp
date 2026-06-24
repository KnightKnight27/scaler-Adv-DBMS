#include <iostream>
using namespace std;

#define RED true
#define BLACK false

struct Node {
    int   data;
    bool  color;
    Node *left, *right, *parent;

    Node(int val) {
        data = val;
        color = RED; 
        left = right = parent = nullptr;
    }
};

class RBTree {
    Node* root;

    void rotateLeft(Node* x) {
        Node* y  = x->right;
        x->right = y->left;
        if (y->left) y->left->parent = x;
        y->parent = x->parent;
        if      (!x->parent)         root        = y;
        else if (x == x->parent->left) x->parent->left  = y;
        else                           x->parent->right = y;
        y->left   = x;
        x->parent = y;
    }

    void rotateRight(Node* x) {
        Node* y = x->left;
        x->left = y->right;
        if (y->right) y->right->parent = x;
        y->parent = x->parent;
        if      (!x->parent)          root         = y;
        else if (x == x->parent->left) x->parent->left  = y;
        else                           x->parent->right = y;
        y->right  = x;
        x->parent = y;
    }

    void fixInsert(Node* z) {
        while (z->parent && z->parent->color == RED) {

            Node* parent      = z->parent;
            Node* grandparent = parent->parent;

            if (parent == grandparent->left) {
                Node* uncle = grandparent->right; 

                if (uncle && uncle->color == RED) {
                    parent->color      = BLACK;
                    uncle->color       = BLACK;
                    grandparent->color = RED;
                    z = grandparent;
                }
                else {
                    if (z == parent->right) {
                        rotateLeft(parent);
                        z      = parent;
                        parent = z->parent;
                    }
                    rotateRight(grandparent);
                    parent->color      = BLACK;
                    grandparent->color = RED;
                }
            }
            else {
                Node* uncle = grandparent->left;

                if (uncle && uncle->color == RED) {  
                    parent->color      = BLACK;
                    uncle->color       = BLACK;
                    grandparent->color = RED;
                    z = grandparent;
                }
                else {
                    if (z == parent->left) {        
                        rotateRight(parent);
                        z      = parent;
                        parent = z->parent;
                    }
                    rotateLeft(grandparent);     
                    parent->color      = BLACK;
                    grandparent->color = RED;
                }
            }
        }
        root->color = BLACK;    
    }

    void inorder(Node* node) {
        if (!node) return;
        inorder(node->left);
        cout << node->data << "(" << (node->color == RED ? "R" : "B") << ") ";
        inorder(node->right);
    }

public:
    RBTree() { root = nullptr; }

    void insert(int val) {
        Node* z = new Node(val);

        Node* cur    = root;
        Node* parent = nullptr;
        while (cur) {
            parent = cur;
            if (val < cur->data) cur = cur->left;
            else                 cur = cur->right;
        }
        z->parent = parent;
        if      (!parent)        root          = z;
        else if (val < parent->data) parent->left  = z;
        else                         parent->right = z;

        fixInsert(z);
    }

    bool search(int val) {
        Node* cur = root;
        while (cur) {
            if      (val == cur->data) return true;
            else if (val <  cur->data) cur = cur->left;
            else                       cur = cur->right;
        }
        return false;
    }

    void print() {
        inorder(root);
        cout << "\n";
    }
};

int main() {
    RBTree t;

    t.insert(10);
    t.insert(20);
    t.insert(30);
    t.insert(15);
    t.insert(5);

    cout << "In-order (sorted): ";
    t.print();

    cout << "Search 15: " << (t.search(15) ? "Found" : "Not Found") << "\n";
    cout << "Search 99: " << (t.search(99) ? "Found" : "Not Found") << "\n";

    return 0;
}
