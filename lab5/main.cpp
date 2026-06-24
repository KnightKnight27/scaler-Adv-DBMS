#include <iostream>
#include <queue>
#include <string>
#include <vector>

// Lab 5: Red-Black Tree Implementation
// Name: Ankit Kumar
// Roll No: 24BCS10189

enum class Color {
    Red,
    Black
};

struct Node {
    int key;
    Color color;
    Node* left;
    Node* right;
    Node* parent;

    explicit Node(int value)
        : key(value), color(Color::Red), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
public:
    ~RedBlackTree() {
        destroy(root);
    }

    void insert(int key) {
        Node* inserted = bstInsert(key);
        fixInsertion(inserted);
    }

    bool search(int key) const {
        Node* current = root;
        while (current != nullptr) {
            if (key == current->key) {
                return true;
            }
            current = key < current->key ? current->left : current->right;
        }
        return false;
    }

    void inorder() const {
        inorder(root);
        std::cout << '\n';
    }

    void levelOrder() const {
        if (root == nullptr) {
            return;
        }

        std::queue<Node*> q;
        q.push(root);
        while (!q.empty()) {
            Node* node = q.front();
            q.pop();

            std::cout << node->key << "(" << colorName(node->color) << ") ";
            if (node->left != nullptr) {
                q.push(node->left);
            }
            if (node->right != nullptr) {
                q.push(node->right);
            }
        }
        std::cout << '\n';
    }

private:
    Node* root{nullptr};

    static std::string colorName(Color color) {
        return color == Color::Red ? "R" : "B";
    }

    static Color colorOf(Node* node) {
        return node == nullptr ? Color::Black : node->color;
    }

    void rotateLeft(Node* x) {
        Node* y = x->right;
        x->right = y->left;

        if (y->left != nullptr) {
            y->left->parent = x;
        }

        y->parent = x->parent;
        if (x->parent == nullptr) {
            root = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }

        y->left = x;
        x->parent = y;
    }

    void rotateRight(Node* x) {
        Node* y = x->left;
        x->left = y->right;

        if (y->right != nullptr) {
            y->right->parent = x;
        }

        y->parent = x->parent;
        if (x->parent == nullptr) {
            root = y;
        } else if (x == x->parent->right) {
            x->parent->right = y;
        } else {
            x->parent->left = y;
        }

        y->right = x;
        x->parent = y;
    }

    Node* bstInsert(int key) {
        Node* node = new Node(key);
        Node* parent = nullptr;
        Node* current = root;

        while (current != nullptr) {
            parent = current;
            current = key < current->key ? current->left : current->right;
        }

        node->parent = parent;
        if (parent == nullptr) {
            root = node;
        } else if (key < parent->key) {
            parent->left = node;
        } else {
            parent->right = node;
        }

        return node;
    }

    void fixInsertion(Node* node) {
        while (node != root && colorOf(node->parent) == Color::Red) {
            Node* parent = node->parent;
            Node* grandparent = parent->parent;

            if (parent == grandparent->left) {
                Node* uncle = grandparent->right;
                if (colorOf(uncle) == Color::Red) {
                    parent->color = Color::Black;
                    uncle->color = Color::Black;
                    grandparent->color = Color::Red;
                    node = grandparent;
                } else {
                    if (node == parent->right) {
                        node = parent;
                        rotateLeft(node);
                        parent = node->parent;
                        grandparent = parent->parent;
                    }
                    parent->color = Color::Black;
                    grandparent->color = Color::Red;
                    rotateRight(grandparent);
                }
            } else {
                Node* uncle = grandparent->left;
                if (colorOf(uncle) == Color::Red) {
                    parent->color = Color::Black;
                    uncle->color = Color::Black;
                    grandparent->color = Color::Red;
                    node = grandparent;
                } else {
                    if (node == parent->left) {
                        node = parent;
                        rotateRight(node);
                        parent = node->parent;
                        grandparent = parent->parent;
                    }
                    parent->color = Color::Black;
                    grandparent->color = Color::Red;
                    rotateLeft(grandparent);
                }
            }
        }

        root->color = Color::Black;
    }

    void inorder(Node* node) const {
        if (node == nullptr) {
            return;
        }

        inorder(node->left);
        std::cout << node->key << "(" << colorName(node->color) << ") ";
        inorder(node->right);
    }

    void destroy(Node* node) {
        if (node == nullptr) {
            return;
        }

        destroy(node->left);
        destroy(node->right);
        delete node;
    }
};

int main() {
    RedBlackTree tree;
    std::vector<int> values{10, 20, 30, 15, 25, 5, 1, 50, 60, 70};

    for (int value : values) {
        tree.insert(value);
    }

    std::cout << "Inorder traversal:\n";
    tree.inorder();

    std::cout << "Level order traversal:\n";
    tree.levelOrder();

    std::cout << "Search 25: " << (tree.search(25) ? "found" : "not found") << '\n';
    std::cout << "Search 99: " << (tree.search(99) ? "found" : "not found") << '\n';

    return 0;
}
