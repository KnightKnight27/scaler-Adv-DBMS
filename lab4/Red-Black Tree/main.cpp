/*
* ==========================================================
 * Lab 4 - Red Black Tree Implementation
 *
 * Name  : Jatin Chulet
 * Roll  : 24BCS10213
 *
 * A Red-Black Tree is a self-balancing Binary Search Tree
 * that guarantees O(log n) insertion and search operations.
 * ==========================================================
 */


enum NodeColor { RED, BLACK };

// Structure representing a node of the Red-Black Tree
struct TreeNode {
    int value;
    NodeColor color;
    TreeNode* left;
    TreeNode* right;
    TreeNode* parent;
};

class RedBlackTree {
public:
    RedBlackTree() {
        // Create NIL sentinel node (always black)
        nil = new TreeNode{0, BLACK, nullptr, nullptr, nullptr};
        root = nil;
    }

    void insert(int value) {

        // New nodes are always inserted as RED
        TreeNode* newNode =
            new TreeNode{value, RED, nil, nil, nil};

        TreeNode* parentNode = nil;
        TreeNode* current = root;

        // Standard BST insertion
        while (current != nil) {
            parentNode = current;

            if (value < current->value)
                current = current->left;
            else
                current = current->right;
        }

        newNode->parent = parentNode;

        // Attach node at appropriate position
        if (parentNode == nil)
            root = newNode;
        else if (value < parentNode->value)
            parentNode->left = newNode;
        else
            parentNode->right = newNode;

        // Restore Red-Black properties
        balanceAfterInsert(newNode);
    }

    bool search(int value) const {

        TreeNode* current = root;

        // Normal BST search
        while (current != nil) {

            if (value == current->value)
                return true;

            if (value < current->value)
                current = current->left;
            else
                current = current->right;
        }

        return false;
    }

private:
    TreeNode* root;
    TreeNode* nil;

    // Left rotation around a node
    void leftRotate(TreeNode* node) {

        TreeNode* child = node->right;

        node->right = child->left;

        if (child->left != nil)
            child->left->parent = node;

        child->parent = node->parent;

        if (node->parent == nil)
            root = child;
        else if (node == node->parent->left)
            node->parent->left = child;
        else
            node->parent->right = child;

        child->left = node;
        node->parent = child;
    }

    // Right rotation around a node
    void rightRotate(TreeNode* node) {

        TreeNode* child = node->left;

        node->left = child->right;

        if (child->right != nil)
            child->right->parent = node;

        child->parent = node->parent;

        if (node->parent == nil)
            root = child;
        else if (node == node->parent->right)
            node->parent->right = child;
        else
            node->parent->left = child;

        child->right = node;
        node->parent = child;
    }

    // Fix violations created after insertion
    void balanceAfterInsert(TreeNode* node) {

        while (node->parent->color == RED) {

            TreeNode* grandParent = node->parent->parent;

            if (node->parent == grandParent->left) {

                TreeNode* uncle = grandParent->right;

                // Case 1: Uncle is RED → recolor
                if (uncle->color == RED) {

                    node->parent->color = BLACK;
                    uncle->color = BLACK;
                    grandParent->color = RED;

                    node = grandParent;
                }
                else {

                    // Case 2: Triangle formation
                    if (node == node->parent->right) {
                        node = node->parent;
                        leftRotate(node);
                    }

                    // Case 3: Line formation
                    node->parent->color = BLACK;
                    grandParent->color = RED;

                    rightRotate(grandParent);
                }
            }
            else {

                // Mirror image cases
                TreeNode* uncle = grandParent->left;

                if (uncle->color == RED) {

                    node->parent->color = BLACK;
                    uncle->color = BLACK;
                    grandParent->color = RED;

                    node = grandParent;
                }
                else {

                    if (node == node->parent->left) {
                        node = node->parent;
                        rightRotate(node);
                    }

                    node->parent->color = BLACK;
                    grandParent->color = RED;

                    leftRotate(grandParent);
                }
            }
        }

        // Root must always remain black
        root->color = BLACK;
    }

    // Inorder traversal prints elements in sorted order
    void inorderTraversal(TreeNode* node) const {

        if (node == nil)
            return;

        inorderTraversal(node->left);

        std::cout
            << node->value
            << (node->color == RED ? "(R) " : "(B) ");

        inorderTraversal(node->right);
    }

    // Verify all Red-Black Tree properties
    bool verifyProperties(
        TreeNode* node,
        int& blackHeight
    ) const {