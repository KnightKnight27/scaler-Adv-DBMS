#include <climits>
#include <iostream>
#include <queue>

enum Color { RED, BLACK };

struct Node {
    int key;
    Color color;
    Node* left;
    Node* right;
    Node* parent;

    Node(int value, Color c, Node* nil)
        : key(value), color(c), left(nil), right(nil), parent(nil) {}
};

class RedBlackTree {
public:
    RedBlackTree() {
        NIL = new Node(0, BLACK, nullptr);
        NIL->left = NIL->right = NIL->parent = NIL;
        root = NIL;
    }

    ~RedBlackTree() {
        clear(root);
        delete NIL;
    }

    void insert(int value) {
        Node* z = new Node(value, RED, NIL);
        Node* y = NIL;
        Node* x = root;

        while (x != NIL) {
            y = x;
            if (z->key < x->key)
                x = x->left;
            else
                x = x->right;
        }

        z->parent = y;
        if (y == NIL) {
            root = z;
        } else if (z->key < y->key) {
            y->left = z;
        } else {
            y->right = z;
        }

        z->left = NIL;
        z->right = NIL;
        z->color = RED;
        insertFixup(z);
    }

    Node* search(int value) const {
        Node* x = root;
        while (x != NIL) {
            if (value == x->key)
                return x;
            x = (value < x->key) ? x->left : x->right;
        }
        return nullptr;
    }

    void printInorder() const {
        std::cout << "Inorder traversal: ";
        inorderPrint(root);
        std::cout << "\n";
    }

    void printLevelOrder() const {
        std::cout << "Level-order traversal: ";
        if (root == NIL) {
            std::cout << "(empty)\n";
            return;
        }

        std::queue<Node*> queue;
        queue.push(root);
        while (!queue.empty()) {
            Node* node = queue.front();
            queue.pop();
            std::cout << node->key << "(" << colorName(node->color) << ") ";
            if (node->left != NIL)
                queue.push(node->left);
            if (node->right != NIL)
                queue.push(node->right);
        }
        std::cout << "\n";
    }

    bool validate() const {
        if (root == NIL) {
            return true;
        }
        if (root->color != BLACK) {
            return false;
        }
        int blackHeight = 0;
        return validateNode(root, INT_MIN, INT_MAX, 0, blackHeight);
    }

private:
    Node* root;
    Node* NIL;

    void leftRotate(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        if (y->left != NIL)
            y->left->parent = x;
        y->parent = x->parent;
        if (x->parent == NIL)
            root = y;
        else if (x == x->parent->left)
            x->parent->left = y;
        else
            x->parent->right = y;
        y->left = x;
        x->parent = y;
    }

    void rightRotate(Node* y) {
        Node* x = y->left;
        y->left = x->right;
        if (x->right != NIL)
            x->right->parent = y;
        x->parent = y->parent;
        if (y->parent == NIL)
            root = x;
        else if (y == y->parent->left)
            y->parent->left = x;
        else
            y->parent->right = x;
        x->right = y;
        y->parent = x;
    }

    void insertFixup(Node* z) {
        while (z->parent->color == RED) {
            if (z->parent == z->parent->parent->left) {
                Node* y = z->parent->parent->right;
                if (y->color == RED) {
                    z->parent->color = BLACK;
                    y->color = BLACK;
                    z->parent->parent->color = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->right) {
                        z = z->parent;
                        leftRotate(z);
                    }
                    z->parent->color = BLACK;
                    z->parent->parent->color = RED;
                    rightRotate(z->parent->parent);
                }
            } else {
                Node* y = z->parent->parent->left;
                if (y->color == RED) {
                    z->parent->color = BLACK;
                    y->color = BLACK;
                    z->parent->parent->color = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        rightRotate(z);
                    }
                    z->parent->color = BLACK;
                    z->parent->parent->color = RED;
                    leftRotate(z->parent->parent);
                }
            }
        }
        root->color = BLACK;
    }

    void inorderPrint(Node* node) const {
        if (node == NIL)
            return;
        inorderPrint(node->left);
        std::cout << node->key << "(" << colorName(node->color) << ") ";
        inorderPrint(node->right);
    }

    bool validateNode(Node* node, int minValue, int maxValue, int blackCount, int& pathBlack) const {
        if (node == NIL) {
            if (pathBlack == 0) {
                pathBlack = blackCount + 1;
                return true;
            }
            return blackCount + 1 == pathBlack;
        }
        if (node->key <= minValue || node->key >= maxValue) {
            return false;
        }
        if (node->color == RED) {
            if (node->left->color == RED || node->right->color == RED)
                return false;
        }
        if (node->color == BLACK)
            blackCount++;
        int leftHeight = 0;
        int rightHeight = 0;
        bool leftValid = validateNode(node->left, minValue, node->key, blackCount, leftHeight);
        bool rightValid = validateNode(node->right, node->key, maxValue, blackCount, rightHeight);
        if (!leftValid || !rightValid)
            return false;
        if (leftHeight != rightHeight)
            return false;
        pathBlack = leftHeight;
        return true;
    }

    const char* colorName(Color c) const {
        return c == RED ? "R" : "B";
    }

    void clear(Node* node) {
        if (node == NIL)
            return;
        clear(node->left);
        clear(node->right);
        delete node;
    }
};

int main() {
    RedBlackTree tree;
    int values[] = {41, 38, 31, 12, 19, 8, 1, 2, 3, 7, 11, 18, 29, 22, 20, 21};
    for (int value : values) {
        tree.insert(value);
    }

    std::cout << "Red-Black Tree validation: " << (tree.validate() ? "VALID" : "INVALID") << "\n";
    tree.printInorder();
    tree.printLevelOrder();

    int searchValue = 19;
    std::cout << "Search " << searchValue << ": " << (tree.search(searchValue) ? "FOUND" : "NOT FOUND") << "\n";
    int missingValue = 100;
    std::cout << "Search " << missingValue << ": " << (tree.search(missingValue) ? "FOUND" : "NOT FOUND") << "\n";
    return 0;
}
