#include <iostream>

enum class NodeColor {
    RED,
    BLACK
};

struct RBNode {
    int value;
    NodeColor color;
    RBNode* left;
    RBNode* right;
    RBNode* parent;

    explicit RBNode(int node_value)
        : value(node_value),
          color(NodeColor::RED),
          left(nullptr),
          right(nullptr),
          parent(nullptr) {}
};

class RedBlackTree {
private:
    RBNode* root_;

    // Left Rotation
    void rotateLeft(RBNode* x) {
        RBNode* y = x->right;

        x->right = y->left;

        if (y->left != nullptr) {
            y->left->parent = x;
        }

        y->parent = x->parent;

        if (x->parent == nullptr) {
            root_ = y;
        }
        else if (x == x->parent->left) {
            x->parent->left = y;
        }
        else {
            x->parent->right = y;
        }

        y->left = x;
        x->parent = y;
    }

    // Right Rotation
    void rotateRight(RBNode* y) {
        RBNode* x = y->left;

        y->left = x->right;

        if (x->right != nullptr) {
            x->right->parent = y;
        }

        x->parent = y->parent;

        if (y->parent == nullptr) {
            root_ = x;
        }
        else if (y == y->parent->left) {
            y->parent->left = x;
        }
        else {
            y->parent->right = x;
        }

        x->right = y;
        y->parent = x;
    }

    // Fix violations after insertion
    void fixInsertViolation(RBNode* node) {
        while (node != root_ &&
               node->parent != nullptr &&
               node->parent->color == NodeColor::RED) {

            RBNode* parent = node->parent;
            RBNode* grandparent = parent->parent;

            // Parent is left child
            if (parent == grandparent->left) {

                RBNode* uncle = grandparent->right;

                // Case 1: Uncle is RED
                if (uncle != nullptr &&
                    uncle->color == NodeColor::RED) {

                    parent->color = NodeColor::BLACK;
                    uncle->color = NodeColor::BLACK;
                    grandparent->color = NodeColor::RED;

                    node = grandparent;
                }
                else {
                    // Case 2: LR
                    if (node == parent->right) {
                        node = parent;
                        rotateLeft(node);

                        parent = node->parent;
                        grandparent = parent->parent;
                    }

                    // Case 3: LL
                    parent->color = NodeColor::BLACK;
                    grandparent->color = NodeColor::RED;

                    rotateRight(grandparent);
                }
            }

            // Parent is right child
            else {

                RBNode* uncle = grandparent->left;

                // Case 1: Uncle is RED
                if (uncle != nullptr &&
                    uncle->color == NodeColor::RED) {

                    parent->color = NodeColor::BLACK;
                    uncle->color = NodeColor::BLACK;
                    grandparent->color = NodeColor::RED;

                    node = grandparent;
                }
                else {
                    // Case 2: RL
                    if (node == parent->left) {
                        node = parent;
                        rotateRight(node);

                        parent = node->parent;
                        grandparent = parent->parent;
                    }

                    // Case 3: RR
                    parent->color = NodeColor::BLACK;
                    grandparent->color = NodeColor::RED;

                    rotateLeft(grandparent);
                }
            }
        }

        root_->color = NodeColor::BLACK;
    }

    void inorderTraversal(RBNode* node) const {
        if (node == nullptr) {
            return;
        }

        inorderTraversal(node->left);

        std::cout << node->value;

        if (node->color == NodeColor::RED) {
            std::cout << "(R) ";
        }
        else {
            std::cout << "(B) ";
        }

        inorderTraversal(node->right);
    }

    void deleteTree(RBNode* node) {
        if (node == nullptr) {
            return;
        }

        deleteTree(node->left);
        deleteTree(node->right);

        delete node;
    }

public:
    RedBlackTree() : root_(nullptr) {}

    ~RedBlackTree() {
        deleteTree(root_);
    }

    void insert(int value) {

        RBNode* newNode = new RBNode(value);

        RBNode* parent = nullptr;
        RBNode* current = root_;

        // BST insertion
        while (current != nullptr) {
            parent = current;

            if (value < current->value) {
                current = current->left;
            }
            else {
                current = current->right;
            }
        }

        newNode->parent = parent;

        if (parent == nullptr) {
            root_ = newNode;
        }
        else if (value < parent->value) {
            parent->left = newNode;
        }
        else {
            parent->right = newNode;
        }

        fixInsertViolation(newNode);
    }

    void printTree() const {
        inorderTraversal(root_);
        std::cout << '\n';
    }
};

int main() {

    RedBlackTree tree;

    tree.insert(10);
    tree.insert(20);
    tree.insert(30);
    tree.insert(15);
    tree.insert(5);
    tree.insert(1);

    tree.printTree();

    return 0;
}