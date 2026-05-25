/**
 * RBTree - A structurally distinct Red-Black Tree implementation in C++
 */

#include <iostream>
#include <string>
#include <queue>
#include <iomanip>
#include <stdexcept>
#include <vector>
#include <algorithm>

// ──────────────────────────────────────────────────────────────────────────────
// Scoped Enum for Node Colors
// ──────────────────────────────────────────────────────────────────────────────
enum class NodeColor { Red, Black };

// ──────────────────────────────────────────────────────────────────────────────
// Tree Node Structure
// ──────────────────────────────────────────────────────────────────────────────
struct TreeNode {
    int key;
    NodeColor color;
    TreeNode* left;
    TreeNode* right;
    TreeNode* parent;

    explicit TreeNode(int val)
        : key(val), color(NodeColor::Red),
          left(nullptr), right(nullptr), parent(nullptr) {}
};

// ──────────────────────────────────────────────────────────────────────────────
// Red-Black Tree Class
// ──────────────────────────────────────────────────────────────────────────────
class RBTree {
private:
    TreeNode* root;
    TreeNode* nilSentinel; // Dedicated black sentinel leaf node

    // ── Structural Helpers ───────────────────────────────────────────────────

    TreeNode* initSentinelNode() {
        TreeNode* sNode = new TreeNode(0);
        sNode->color = NodeColor::Black;
        sNode->left = sNode;
        sNode->right = sNode;
        sNode->parent = sNode;
        return sNode;
    }

    // ── Tree Rotations ───────────────────────────────────────────────────────

    void pivotLeft(TreeNode* curr) {
        TreeNode* child = curr->right;
        curr->right = child->left;

        if (child->left != nilSentinel) {
            child->left->parent = curr;
        }

        child->parent = curr->parent;

        if (curr->parent == nilSentinel) {
            root = child;
        } else if (curr == curr->parent->left) {
            curr->parent->left = child;
        } else {
            curr->parent->right = child;
        }

        child->left = curr;
        curr->parent = child;
    }

    void pivotRight(TreeNode* curr) {
        TreeNode* child = curr->left;
        curr->left = child->right;

        if (child->right != nilSentinel) {
            child->right->parent = curr;
        }

        child->parent = curr->parent;

        if (curr->parent == nilSentinel) {
            root = child;
        } else if (curr == curr->parent->right) {
            curr->parent->right = child;
        } else {
            curr->parent->left = child;
        }

        child->right = curr;
        curr->parent = child;
    }

    // ── Rebalancing Routines ─────────────────────────────────────────────────

    void balanceInsertion(TreeNode* curr) {
        while (curr->parent->color == NodeColor::Red) {
            if (curr->parent == curr->parent->parent->left) {
                TreeNode* uncle = curr->parent->parent->right;

                if (uncle->color == NodeColor::Red) {
                    curr->parent->color = NodeColor::Black;
                    uncle->color = NodeColor::Black;
                    curr->parent->parent->color = NodeColor::Red;
                    curr = curr->parent->parent;
                } else {
                    if (curr == curr->parent->right) {
                        curr = curr->parent;
                        pivotLeft(curr);
                    }
                    curr->parent->color = NodeColor::Black;
                    curr->parent->parent->color = NodeColor::Red;
                    pivotRight(curr->parent->parent);
                }
            } else {
                TreeNode* uncle = curr->parent->parent->left;

                if (uncle->color == NodeColor::Red) {
                    curr->parent->color = NodeColor::Black;
                    uncle->color = NodeColor::Black;
                    curr->parent->parent->color = NodeColor::Red;
                    curr = curr->parent->parent;
                } else {
                    if (curr == curr->parent->left) {
                        curr = curr->parent;
                        pivotRight(curr);
                    }
                    curr->parent->color = NodeColor::Black;
                    curr->parent->parent->color = NodeColor::Red;
                    pivotLeft(curr->parent->parent);
                }
            }
        }
        root->color = NodeColor::Black;
    }

    void balanceDeletion(TreeNode* curr) {
        while (curr != root && curr->color == NodeColor::Black) {
            if (curr == curr->parent->left) {
                TreeNode* sibling = curr->parent->right;

                if (sibling->color == NodeColor::Red) {
                    sibling->color = NodeColor::Black;
                    curr->parent->color = NodeColor::Red;
                    pivotLeft(curr->parent);
                    sibling = curr->parent->right;
                }

                if (sibling->left->color == NodeColor::Black && sibling->right->color == NodeColor::Black) {
                    sibling->color = NodeColor::Red;
                    curr = curr->parent;
                } else {
                    if (sibling->right->color == NodeColor::Black) {
                        sibling->left->color = NodeColor::Black;
                        sibling->color = NodeColor::Red;
                        pivotRight(sibling);
                        sibling = curr->parent->right;
                    }
                    sibling->color = curr->parent->color;
                    curr->parent->color = NodeColor::Black;
                    sibling->right->color = NodeColor::Black;
                    pivotLeft(curr->parent);
                    curr = root;
                }
            } else {
                TreeNode* sibling = curr->parent->left;

                if (sibling->color == NodeColor::Red) {
                    sibling->color = NodeColor::Black;
                    curr->parent->color = NodeColor::Red;
                    pivotRight(curr->parent);
                    sibling = curr->parent->left;
                }

                if (sibling->right->color == NodeColor::Black && sibling->left->color == NodeColor::Black) {
                    sibling->color = NodeColor::Red;
                    curr = curr->parent;
                } else {
                    if (sibling->left->color == NodeColor::Black) {
                        sibling->right->color = NodeColor::Black;
                        sibling->color = NodeColor::Red;
                        pivotLeft(sibling);
                        sibling = curr->parent->left;
                    }
                    sibling->color = curr->parent->color;
                    curr->parent->color = NodeColor::Black;
                    sibling->left->color = NodeColor::Black;
                    pivotRight(curr->parent);
                    curr = root;
                }
            }
        }
        curr->color = NodeColor::Black;
    }

    void shiftNodes(TreeNode* oldNode, TreeNode* newNode) {
        if (oldNode->parent == nilSentinel) {
            root = newNode;
        } else if (oldNode == oldNode->parent->left) {
            oldNode->parent->left = newNode;
        } else {
            oldNode->parent->right = newNode;
        }
        newNode->parent = oldNode->parent;
    }

    TreeNode* getMinimum(TreeNode* node) const {
        while (node->left != nilSentinel) {
            node = node->left;
        }
        return node;
    }

    TreeNode* getMaximum(TreeNode* node) const {
        while (node->right != nilSentinel) {
            node = node->right;
        }
        return node;
    }

    // ── Recurse Traversal Helpers ────────────────────────────────────────────

    void showInorder(TreeNode* node) const {
        if (node == nilSentinel) return;
        showInorder(node->left);
        std::cout << node->key << " ";
        showInorder(node->right);
    }

    void showPreorder(TreeNode* node) const {
        if (node == nilSentinel) return;
        std::cout << node->key << " ";
        showPreorder(node->left);
        showPreorder(node->right);
    }

    void showPostorder(TreeNode* node) const {
        if (node == nilSentinel) return;
        showPostorder(node->left);
        showPostorder(node->right);
        std::cout << node->key << " ";
    }

    int checkValidity(TreeNode* node) const {
        if (node == nilSentinel) return 1;

        if (node->color == NodeColor::Red) {
            if (node->left->color == NodeColor::Red || node->right->color == NodeColor::Red) {
                return -1;
            }
        }

        int leftBH = checkValidity(node->left);
        int rightBH = checkValidity(node->right);

        if (leftBH == -1 || rightBH == -1 || leftBH != rightBH) return -1;

        return leftBH + (node->color == NodeColor::Black ? 1 : 0);
    }

    void renderTree(TreeNode* node, const std::string& gap, bool isRightSide) const {
        if (node == nilSentinel) return;

        std::cout << gap << (isRightSide ? "R└── " : "L└── ") << node->key 
                  << (node->color == NodeColor::Red ? "(R)" : "(B)") << "\n";

        std::string nextGap = gap + (isRightSide ? "    " : "│   ");
        renderTree(node->left, nextGap, false);
        renderTree(node->right, nextGap, true);
    }

    void clearMemory(TreeNode* node) {
        if (node == nilSentinel) return;
        clearMemory(node->left);
        clearMemory(node->right);
        delete node;
    }

    int determineHeight(TreeNode* node) const {
        if (node == nilSentinel) return -1;
        return 1 + std::max(determineHeight(node->left), determineHeight(node->right));
    }

    int determineSize(TreeNode* node) const {
        if (node == nilSentinel) return 0;
        return 1 + determineSize(node->left) + determineSize(node->right);
    }

public:
    RBTree() {
        nilSentinel = initSentinelNode();
        root = nilSentinel;
    }

    ~RBTree() {
        clearMemory(root);
        delete nilSentinel;
    }

    void insert(int key) {
        TreeNode* target = new TreeNode(key);
        target->left = nilSentinel;
        target->right = nilSentinel;

        TreeNode* trailing = nilSentinel;
        TreeNode* current = root;

        while (current != nilSentinel) {
            trailing = current;
            if (target->key < current->key) {
                current = current->left;
            } else if (target->key > current->key) {
                current = current->right;
            } else {
                delete target;
                return; // Suppress duplicate inserts
            }
        }

        target->parent = trailing;

        if (trailing == nilSentinel) {
            root = target;
        } else if (target->key < trailing->key) {
            trailing->left = target;
        } else {
            trailing->right = target;
        }

        balanceInsertion(target);
    }

    void remove(int key) {
        TreeNode* match = findNode(root, key);
        if (match == nilSentinel) {
            std::cerr << "Key " << key << " absent.\n";
            return;
        }

        TreeNode* spliceNode = match;
        TreeNode* replacementNode;
        NodeColor droppedColor = spliceNode->color;

        if (match->left == nilSentinel) {
            replacementNode = match->right;
            shiftNodes(match, match->right);
        } else if (match->right == nilSentinel) {
            replacementNode = match->left;
            shiftNodes(match, match->left);
        } else {
            spliceNode = getMinimum(match->right);
            droppedColor = spliceNode->color;
            replacementNode = spliceNode->right;

            if (spliceNode->parent == match) {
                replacementNode->parent = spliceNode;
            } else {
                shiftNodes(spliceNode, spliceNode->right);
                spliceNode->right = match->right;
                spliceNode->right->parent = spliceNode;
            }

            shiftNodes(match, spliceNode);
            spliceNode->left = match->left;
            spliceNode->left->parent = spliceNode;
            spliceNode->color = match->color;
        }

        delete match;

        if (droppedColor == NodeColor::Black) {
            balanceDeletion(replacementNode);
        }
    }

    bool search(int key) const {
        return findNode(root, key) != nilSentinel;
    }

    TreeNode* findNode(TreeNode* node, int key) const {
        while (node != nilSentinel && key != node->key) {
            node = (key < node->key) ? node->left : node->right;
        }
        return node;
    }

    int fetchMin() const {
        if (isEmpty()) throw std::runtime_error("Empty Tree.");
        return getMinimum(root)->key;
    }

    int fetchMax() const {
        if (isEmpty()) throw std::runtime_error("Empty Tree.");
        return getMaximum(root)->key;
    }

    bool isEmpty() const {
        return root == nilSentinel;
    }

    int height() const {
        return determineHeight(root);
    }

    int size() const {
        return determineSize(root);
    }

    int blackHeight() const {
        int bh = 0;
        TreeNode* current = root;
        while (current != nilSentinel) {
            if (current->color == NodeColor::Black) ++bh;
            current = current->left;
        }
        return bh;
    }

    bool isValid() const {
        if (root == nilSentinel) return true;
        if (root->color != NodeColor::Black) return false;
        return checkValidity(root) != -1;
    }

    void printTreeStructure() const {
        if (isEmpty()) {
            std::cout << "[Empty Tree]\n";
            return;
        }
        renderTree(root, "", true);
    }

    void displayTraversals() const {
        std::cout << "Inorder Traverse:   "; showInorder(root); std::cout << "\n";
        std::cout << "Preorder Traverse:  "; showPreorder(root); std::cout << "\n";
        std::cout << "Postorder Traverse: "; showPostorder(root); std::cout << "\n";
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// Demonstration Runner
// ──────────────────────────────────────────────────────────────────────────────
int main() {
    RBTree tree;

    std::cout << "=== Section 1: Populating Tree ===\n";
    std::vector<int> sourceKeys = {10, 20, 30, 15, 25, 5, 1, 7, 40, 35};
    for (int key : sourceKeys) {
        tree.insert(key);
    }
    
    tree.printTreeStructure();
    std::cout << "\n";
    tree.displayTraversals();

    std::cout << "\n=== Section 2: Properties & Verification ===\n";
    std::cout << "Nodes count : " << tree.size() << "\n";
    std::cout << "Max Depth   : " << tree.height() << "\n";
    std::cout << "Black Height: " << tree.blackHeight() << "\n";
    std::cout << "Valid Structure? " << (tree.isValid() ? "YES" : "NO") << "\n";

    std::cout << "\n=== Section 3: Value Removal ===\n";
    std::cout << "Removing element 20...\n";
    tree.remove(20);
    tree.printTreeStructure();
    std::cout << "Valid Structure? " << (tree.isValid() ? "YES" : "NO") << "\n";

    return 0;
}