#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

enum class Shade {
    RED,
    BLACK
};

struct Node {
    int value;
    Shade colour;
    Node* leftChild;
    Node* rightChild;
    Node* parentNode;

    explicit Node(int val)
        : value(val),
          colour(Shade::RED),
          leftChild(nullptr),
          rightChild(nullptr),
          parentNode(nullptr) {}
};

class RedBlackTree {
private:
    Node* rootNode;

public:
    RedBlackTree() {
        rootNode = nullptr;
    }

    ~RedBlackTree() {
        clearTree(rootNode);
    }

    void insertValue(int value) {
        Node* newNode = new Node(value);
        placeNode(newNode);
        fixInsertion(newNode);
    }

    const Node* searchValue(int target) const {
        Node* current = rootNode;

        while (current != nullptr) {
            if (current->value == target) {
                return current;
            }

            if (target < current->value) {
                current = current->leftChild;
            } else {
                current = current->rightChild;
            }
        }

        return nullptr;
    }

    std::vector<int> inorderTraversal() const {
        std::vector<int> output;
        collectValues(rootNode, output);
        return output;
    }

    void printTree() const {
        showTree(rootNode, 0);
    }

    bool validateTree() const {
        if (rootNode == nullptr) {
            return true;
        }

        if (rootNode->colour != Shade::BLACK) {
            return false;
        }

        if (countBlackHeight(rootNode) == -1) {
            return false;
        }

        return verifyBST(rootNode, nullptr, nullptr);
    }

private:
    void clearTree(Node* node) {
        if (node == nullptr) {
            return;
        }

        clearTree(node->leftChild);
        clearTree(node->rightChild);
        delete node;
    }

    void rotateLeft(Node* node) {
        Node* child = node->rightChild;

        node->rightChild = child->leftChild;

        if (child->leftChild != nullptr) {
            child->leftChild->parentNode = node;
        }

        child->parentNode = node->parentNode;

        if (node->parentNode == nullptr) {
            rootNode = child;
        } else if (node == node->parentNode->leftChild) {
            node->parentNode->leftChild = child;
        } else {
            node->parentNode->rightChild = child;
        }

        child->leftChild = node;
        node->parentNode = child;
    }

    void rotateRight(Node* node) {
        Node* child = node->leftChild;

        node->leftChild = child->rightChild;

        if (child->rightChild != nullptr) {
            child->rightChild->parentNode = node;
        }

        child->parentNode = node->parentNode;

        if (node->parentNode == nullptr) {
            rootNode = child;
        } else if (node == node->parentNode->leftChild) {
            node->parentNode->leftChild = child;
        } else {
            node->parentNode->rightChild = child;
        }

        child->rightChild = node;
        node->parentNode = child;
    }

    void placeNode(Node* node) {
        Node* parent = nullptr;
        Node* current = rootNode;

        while (current != nullptr) {
            parent = current;

            if (node->value < current->value) {
                current = current->leftChild;
            } else {
                current = current->rightChild;
            }
        }

        node->parentNode = parent;

        if (parent == nullptr) {
            rootNode = node;
        } else if (node->value < parent->value) {
            parent->leftChild = node;
        } else {
            parent->rightChild = node;
        }
    }

    void fixInsertion(Node* node) {
        while (node->parentNode != nullptr &&
               node->parentNode->colour == Shade::RED) {

            Node* parent = node->parentNode;
            Node* grandParent = parent->parentNode;

            if (parent == grandParent->leftChild) {

                Node* uncle = grandParent->rightChild;

                if (uncle != nullptr &&
                    uncle->colour == Shade::RED) {

                    parent->colour = Shade::BLACK;
                    uncle->colour = Shade::BLACK;
                    grandParent->colour = Shade::RED;
                    node = grandParent;
                } else {

                    if (node == parent->rightChild) {
                        node = parent;
                        rotateLeft(node);
                        parent = node->parentNode;
                    }

                    parent->colour = Shade::BLACK;
                    grandParent->colour = Shade::RED;
                    rotateRight(grandParent);
                }
            } else {

                Node* uncle = grandParent->leftChild;

                if (uncle != nullptr &&
                    uncle->colour == Shade::RED) {

                    parent->colour = Shade::BLACK;
                    uncle->colour = Shade::BLACK;
                    grandParent->colour = Shade::RED;
                    node = grandParent;
                } else {

                    if (node == parent->leftChild) {
                        node = parent;
                        rotateRight(node);
                        parent = node->parentNode;
                    }

                    parent->colour = Shade::BLACK;
                    grandParent->colour = Shade::RED;
                    rotateLeft(grandParent);
                }
            }
        }

        rootNode->colour = Shade::BLACK;
    }

    static void collectValues(
        const Node* node,
        std::vector<int>& output
    ) {
        if (node == nullptr) {
            return;
        }

        collectValues(node->leftChild, output);
        output.push_back(node->value);
        collectValues(node->rightChild, output);
    }

    static void showTree(
        const Node* node,
        int depth
    ) {
        if (node == nullptr) {
            return;
        }

        showTree(node->rightChild, depth + 1);

        for (int i = 0; i < depth; i++) {
            std::cout << "    ";
        }

        std::cout << node->value
                  << (node->colour == Shade::RED ? "(R)" : "(B)")
                  << "\n";

        showTree(node->leftChild, depth + 1);
    }

    static int countBlackHeight(const Node* node) {
        if (node == nullptr) {
            return 1;
        }

        if (node->colour == Shade::RED) {
            if ((node->leftChild != nullptr &&
                 node->leftChild->colour == Shade::RED) ||

                (node->rightChild != nullptr &&
                 node->rightChild->colour == Shade::RED)) {

                return -1;
            }
        }

        int leftHeight = countBlackHeight(node->leftChild);
        int rightHeight = countBlackHeight(node->rightChild);

        if (leftHeight == -1 ||
            rightHeight == -1 ||
            leftHeight != rightHeight) {

            return -1;
        }

        return leftHeight +
               (node->colour == Shade::BLACK ? 1 : 0);
    }

    static bool verifyBST(
        const Node* node,
        const int* low,
        const int* high
    ) {
        if (node == nullptr) {
            return true;
        }

        if (low != nullptr &&
            node->value <= *low) {
            return false;
        }

        if (high != nullptr &&
            node->value >= *high) {
            return false;
        }

        return verifyBST(
                   node->leftChild,
                   low,
                   &node->value
               ) &&
               verifyBST(
                   node->rightChild,
                   &node->value,
                   high
               );
    }
};

int main() {

    RedBlackTree tree;

    std::vector<int> values =
    {15, 28, 44, 19, 35, 8, 4, 11, 52, 47};

    std::cout << "Adding values: ";

    for (int value : values) {
        std::cout << value << " ";
        tree.insertValue(value);
    }

    std::cout
        << "\n\nTree Structure:\n";

    tree.printTree();

    std::cout
        << "\nSorted Traversal:\n";

    auto ordered =
        tree.inorderTraversal();

    for (int item : ordered) {
        std::cout << item << " ";
    }

    std::cout
        << "\n\nSearch Results:\n";

    for (int query :
         {19, 100, 4, 90}) {

        std::cout
            << "find("
            << query
            << ") -> "
            << (tree.searchValue(query)
                ? "present"
                : "absent")
            << "\n";
    }

    bool valid =
        tree.validateTree();

    std::cout
        << "\nTree Properties Valid: "
        << (valid ? "yes" : "no")
        << "\n";

    assert(valid);

    std::vector<int> expected = values;
    std::sort(
        expected.begin(),
        expected.end()
    );

    assert(expected == ordered);

    return 0;
}
