#include <iostream>

enum Color {
    Red,
    Black
};

class TreeNode {
public:
    int value;
    Color tone;
    TreeNode* left;
    TreeNode* right;
    TreeNode* parent;

    TreeNode(int v)
        : value(v),
          tone(Red),
          left(nullptr),
          right(nullptr),
          parent(nullptr) {}
};

class RedBlackStructure {
private:
    TreeNode* rootNode;

    bool isRed(TreeNode* node) {
        return node && node->tone == Red;
    }

    void shiftLeft(TreeNode* current) {
        TreeNode* pivot = current->right;

        current->right = pivot->left;

        if (pivot->left) {
            pivot->left->parent = current;
        }

        pivot->parent = current->parent;

        if (!current->parent) {
            rootNode = pivot;
        } else if (current == current->parent->left) {
            current->parent->left = pivot;
        } else {
            current->parent->right = pivot;
        }

        pivot->left = current;
        current->parent = pivot;
    }

    void shiftRight(TreeNode* current) {
        TreeNode* pivot = current->left;

        current->left = pivot->right;

        if (pivot->right) {
            pivot->right->parent = current;
        }

        pivot->parent = current->parent;

        if (!current->parent) {
            rootNode = pivot;
        } else if (current == current->parent->right) {
            current->parent->right = pivot;
        } else {
            current->parent->left = pivot;
        }

        pivot->right = current;
        current->parent = pivot;
    }

    void balanceAfterInsert(TreeNode* node) {

        while (node->parent && isRed(node->parent)) {

            TreeNode* parentNode = node->parent;
            TreeNode* grandNode  = parentNode->parent;

            if (parentNode == grandNode->left) {

                TreeNode* uncleNode = grandNode->right;

                if (isRed(uncleNode)) {

                    parentNode->tone = Black;
                    uncleNode->tone = Black;
                    grandNode->tone = Red;

                    node = grandNode;

                } else {

                    if (node == parentNode->right) {
                        node = parentNode;
                        shiftLeft(node);
                        parentNode = node->parent;
                    }

                    shiftRight(grandNode);

                    parentNode->tone = Black;
                    grandNode->tone = Red;
                }

            } else {

                TreeNode* uncleNode = grandNode->left;

                if (isRed(uncleNode)) {

                    parentNode->tone = Black;
                    uncleNode->tone = Black;
                    grandNode->tone = Red;

                    node = grandNode;

                } else {

                    if (node == parentNode->left) {
                        node = parentNode;
                        shiftRight(node);
                        parentNode = node->parent;
                    }

                    shiftLeft(grandNode);

                    parentNode->tone = Black;
                    grandNode->tone = Red;
                }
            }
        }

        rootNode->tone = Black;
    }

    void displayInorder(TreeNode* node) {

        if (!node) {
            return;
        }

        displayInorder(node->left);

        std::cout << node->value
                  << "["
                  << (node->tone == Red ? "R" : "B")
                  << "] ";

        displayInorder(node->right);
    }

public:
    RedBlackStructure() : rootNode(nullptr) {}

    void addValue(int data) {

        TreeNode* freshNode = new TreeNode(data);

        TreeNode* previous = nullptr;
        TreeNode* current = rootNode;

        while (current) {

            previous = current;

            if (data < current->value) {
                current = current->left;
            } else {
                current = current->right;
            }
        }

        freshNode->parent = previous;

        if (!previous) {
            rootNode = freshNode;
        } else if (data < previous->value) {
            previous->left = freshNode;
        } else {
            previous->right = freshNode;
        }

        balanceAfterInsert(freshNode);
    }

    bool contains(int target) {

        TreeNode* walker = rootNode;

        while (walker) {

            if (target == walker->value) {
                return true;
            }

            if (target < walker->value) {
                walker = walker->left;
            } else {
                walker = walker->right;
            }
        }

        return false;
    }

    void showTree() {
        displayInorder(rootNode);
        std::cout << std::endl;
    }
};

int main() {

    RedBlackStructure rb;

    rb.addValue(40);
    rb.addValue(25);
    rb.addValue(60);
    rb.addValue(10);
    rb.addValue(35);

    std::cout << "Tree Traversal: ";
    rb.showTree();

    std::cout << "Finding 35 -> "
              << (rb.contains(35) ? "Exists" : "Missing")
              << std::endl;

    std::cout << "Finding 90 -> "
              << (rb.contains(90) ? "Exists" : "Missing")
              << std::endl;

    return 0;
}
