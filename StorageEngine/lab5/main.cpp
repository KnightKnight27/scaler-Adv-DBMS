#include <iostream>
#include <memory>
#include <vector>

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

    explicit RBNode(const int node_value)
        : value(node_value), color(NodeColor::RED), left(nullptr),
          right(nullptr), parent(nullptr) {
    }

    ~RBNode() = default;
};

class RedBlackTree {
private:
    RBNode* root_;

    // Perform left rotation around the given node
    // In a left rotation: x -> y, x becomes left child of y, y->left becomes x->right
    void rotateLeft(RBNode* rotation_node) {
        RBNode* right_child = rotation_node->right;

        rotation_node->right = right_child->left;

        if (right_child->left != nullptr) {
            right_child->left->parent = rotation_node;
        }

        right_child->parent = rotation_node->parent;

        if (rotation_node->parent == nullptr) {
            root_ = right_child;
        } else if (rotation_node == rotation_node->parent->left) {
            rotation_node->parent->left = right_child;
        } else {
            rotation_node->parent->right = right_child;
        }

        right_child->left = rotation_node;
        rotation_node->parent = right_child;
    }

    // Perform right rotation around the given node
    // In a right rotation: y -> x, y becomes right child of x, x->right becomes y->left
    void rotateRight(RBNode* rotation_node) {
        RBNode* left_child = rotation_node->left;

        rotation_node->left = left_child->right;

        if (left_child->right != nullptr) {
            left_child->right->parent = rotation_node;
        }

        left_child->parent = rotation_node->parent;

        if (rotation_node->parent == nullptr) {
            root_ = left_child;
        } else if (rotation_node == rotation_node->parent->left) {
            rotation_node->parent->left = left_child;
        } else {
            rotation_node->parent->right = left_child;
        }

        left_child->right = rotation_node;
        rotation_node->parent = left_child;
    }

    // Fix RB-tree violations after insertion
    // Handles three violation cases:
    // Case 1: Uncle is RED - recolor and move violation up
    // Case 2: Uncle is BLACK, node is inner child - rotate to convert to Case 3
    // Case 3: Uncle is BLACK, node is outer child - rotate and recolor
    void fixInsertViolation(RBNode* violation_node) {
        while (violation_node != root_ && violation_node->parent->color == NodeColor::RED) {
            RBNode* parent_node = violation_node->parent;
            RBNode* grandparent_node = parent_node->parent;

            // Case: parent is left child of grandparent
            if (parent_node == grandparent_node->left) {
                RBNode* uncle_node = grandparent_node->right;

                // Case 1: Uncle is RED - recolor parent, uncle, and grandparent
                if (uncle_node != nullptr && uncle_node->color == NodeColor::RED) {
                    parent_node->color = NodeColor::BLACK;
                    uncle_node->color = NodeColor::BLACK;
                    grandparent_node->color = NodeColor::RED;
                    violation_node = grandparent_node;
                } else {
                    // Case 2: LR configuration - rotate parent left to create LL
                    if (violation_node == parent_node->right) {
                        rotateLeft(parent_node);
                        violation_node = parent_node;
                        parent_node = violation_node->parent;
                    }

                    // Case 3: LL configuration - rotate grandparent right and recolor
                    rotateRight(grandparent_node);
                    NodeColor temp_color = parent_node->color;
                    parent_node->color = grandparent_node->color;
                    grandparent_node->color = temp_color;
                }
            }
            // Case: parent is right child of grandparent
            else {
                RBNode* uncle_node = grandparent_node->left;

                // Case 1: Uncle is RED - recolor parent, uncle, and grandparent
                if (uncle_node != nullptr && uncle_node->color == NodeColor::RED) {
                    parent_node->color = NodeColor::BLACK;
                    uncle_node->color = NodeColor::BLACK;
                    grandparent_node->color = NodeColor::RED;
                    violation_node = grandparent_node;
                } else {
                    // Case 2: RL configuration - rotate parent right to create RR
                    if (violation_node == parent_node->left) {
                        rotateRight(parent_node);
                        violation_node = parent_node;
                        parent_node = violation_node->parent;
                    }

                    // Case 3: RR configuration - rotate grandparent left and recolor
                    rotateLeft(grandparent_node);
                    NodeColor temp_color = parent_node->color;
                    parent_node->color = grandparent_node->color;
                    grandparent_node->color = temp_color;
                }
            }
        }

        root_->color = NodeColor::BLACK;
    }

    // Perform inorder traversal and print node values with colors
    void inorderTraversal(const RBNode* node) const {
        if (node == nullptr) {
            return;
        }

        inorderTraversal(node->left);

        std::cout << node->value;
        if (node->color == NodeColor::RED) {
            std::cout << "(R) ";
        } else {
            std::cout << "(B) ";
        }

        inorderTraversal(node->right);
    }

public:
    RedBlackTree() : root_(nullptr) {
    }

    ~RedBlackTree() {
        deleteTree(root_);
    }

    // Insert a new value into the Red-Black tree
    void insert(const int new_value) {
        RBNode* new_node = new RBNode(new_value);

        RBNode* parent_node = nullptr;
        RBNode* current_node = root_;

        // Perform standard BST insertion to find correct position
        while (current_node != nullptr) {
            parent_node = current_node;

            if (new_node->value < current_node->value) {
                current_node = current_node->left;
            } else {
                current_node = current_node->right;
            }
        }

        new_node->parent = parent_node;

        if (parent_node == nullptr) {
            root_ = new_node;
        } else if (new_node->value < parent_node->value) {
            parent_node->left = new_node;
        } else {
            parent_node->right = new_node;
        }

        // Fix RB-tree property violations
        fixInsertViolation(new_node);
    }

    // Print the tree in inorder traversal with color indicators
    void printTree() const {
        inorderTraversal(root_);
        std::cout << std::endl;
    }

private:
    // Helper function to delete all nodes in the tree (cleanup in destructor)
    void deleteTree(RBNode* node) {
        if (node == nullptr) {
            return;
        }
        deleteTree(node->left);
        deleteTree(node->right);
        delete node;
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
