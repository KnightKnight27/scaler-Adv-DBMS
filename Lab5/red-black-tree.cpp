#include <iostream>
#include <string>

// colors
#define RED   0
#define BLACK 1

struct Node {
    int   data;
    int   color;
    Node* left;
    Node* right;
    Node* parent;

    Node(int val) {
        data   = val;
        color  = RED;   // new nodes are always red first
        left   = nullptr;
        right  = nullptr;
        parent = nullptr;
    }
};

class RedBlackTree {
public:
    Node* root;
    Node* NIL;  // sentinel node (acts as all null leaves)

    RedBlackTree() {
        // NIL is a black sentinel, avoids checking nullptr everywhere
        NIL        = new Node(0);
        NIL->color = BLACK;
        NIL->left  = nullptr;
        NIL->right = nullptr;
        root       = NIL;
    }

    // ── INSERT ──────────────────────────────────────────────
    void insert(int val) {
        Node* newNode    = new Node(val);
        newNode->left    = NIL;
        newNode->right   = NIL;
        newNode->parent  = nullptr;

        // normal BST insert first
        Node* parent  = nullptr;
        Node* current = root;

        while (current != NIL) {
            parent = current;
            if (val < current->data)
                current = current->left;
            else if (val > current->data)
                current = current->right;
            else {
                std::cout << "  [insert] " << val << " already exists, skipping\n";
                delete newNode;
                return;
            }
        }

        newNode->parent = parent;

        if (parent == nullptr)
            root = newNode;   // tree was empty
        else if (val < parent->data)
            parent->left = newNode;
        else
            parent->right = newNode;

        std::cout << "  [insert] " << val << " inserted as "
                  << (newNode->color == RED ? "RED" : "BLACK") << "\n";

        // fix red-black properties
        fixInsert(newNode);
    }

    // ── SEARCH ──────────────────────────────────────────────
    bool search(int val) {
        Node*  current     = root;
        int    comparisons = 0;

        while (current != NIL) {
            comparisons++;
            if (val == current->data) {
                std::cout << "  [search] " << val << " FOUND"
                          << " | comparisons: " << comparisons
                          << " | color: " << (current->color == RED ? "RED" : "BLACK") << "\n";
                return true;
            } else if (val < current->data) {
                current = current->left;
            } else {
                current = current->right;
            }
        }

        std::cout << "  [search] " << val << " NOT FOUND"
                  << " | comparisons: " << comparisons << "\n";
        return false;
    }

    // ── INORDER TRAVERSAL ───────────────────────────────────
    void inorder() {
        std::cout << "  Inorder: ";
        inorderHelper(root);
        std::cout << "\n";
    }

    // ── VERIFY PROPERTIES ───────────────────────────────────
    void verifyProperties() {
        std::cout << "\n  --- Property Verification ---\n";

        // Property 1: root must be black
        if (root == NIL) {
            std::cout << "  Tree is empty\n";
            return;
        }
        std::cout << "  Root is "
                  << (root->color == BLACK ? "BLACK [OK]" : "RED [VIOLATION!]") << "\n";

        // Property 2: count black height from root to leaves (must be consistent)
        int blackHeight = 0;
        bool consistent = checkBlackHeight(root, 0, blackHeight);
        std::cout << "  Black height consistent: " << (consistent ? "YES [OK]" : "NO [VIOLATION!]") << "\n";

        // Property 3: no two consecutive red nodes
        bool noConsecRed = checkNoConsecRed(root);
        std::cout << "  No consecutive red nodes: " << (noConsecRed ? "YES [OK]" : "NO [VIOLATION!]") << "\n";
    }

    // ── PRINT TREE ──────────────────────────────────────────
    void printTree() {
        std::cout << "\n  Tree structure (left to right = top to bottom):\n";
        printHelper(root, "", false);
    }

private:
    // ── LEFT ROTATE ─────────────────────────────────────────
    void leftRotate(Node* x) {
        Node* y  = x->right;
        x->right = y->left;

        if (y->left != NIL)
            y->left->parent = x;

        y->parent = x->parent;

        if (x->parent == nullptr)
            root = y;
        else if (x == x->parent->left)
            x->parent->left = y;
        else
            x->parent->right = y;

        y->left   = x;
        x->parent = y;

        std::cout << "    [rotate] LEFT  rotate on " << x->data << "\n";
    }

    // ── RIGHT ROTATE ────────────────────────────────────────
    void rightRotate(Node* x) {
        Node* y = x->left;
        x->left = y->right;

        if (y->right != NIL)
            y->right->parent = x;

        y->parent = x->parent;

        if (x->parent == nullptr)
            root = y;
        else if (x == x->parent->right)
            x->parent->right = y;
        else
            x->parent->left = y;

        y->right  = x;
        x->parent = y;

        std::cout << "    [rotate] RIGHT rotate on " << x->data << "\n";
    }

    // ── FIX INSERT ──────────────────────────────────────────
    void fixInsert(Node* z) {
        // keep fixing while parent is red (red-red violation)
        while (z->parent != nullptr && z->parent->color == RED) {
            Node* grandparent = z->parent->parent;

            if (z->parent == grandparent->left) {
                Node* uncle = grandparent->right;

                if (uncle->color == RED) {
                    // Case 1: uncle is red → recolor
                    std::cout << "    [fix] Case 1 recolor: parent "
                              << z->parent->data << " and uncle "
                              << uncle->data << " → BLACK, grandparent "
                              << grandparent->data << " → RED\n";
                    z->parent->color  = BLACK;
                    uncle->color      = BLACK;
                    grandparent->color = RED;
                    z = grandparent;
                } else {
                    if (z == z->parent->right) {
                        // Case 2: uncle black, z is right child → left rotate
                        z = z->parent;
                        leftRotate(z);
                    }
                    // Case 3: uncle black, z is left child → right rotate
                    std::cout << "    [fix] Case 3 recolor: parent "
                              << z->parent->data << " → BLACK, grandparent "
                              << grandparent->data << " → RED\n";
                    z->parent->color   = BLACK;
                    grandparent->color = RED;
                    rightRotate(grandparent);
                }
            } else {
                // mirror of above (parent is right child)
                Node* uncle = grandparent->left;

                if (uncle->color == RED) {
                    // Case 1 mirror
                    std::cout << "    [fix] Case 1 recolor (mirror): parent "
                              << z->parent->data << " and uncle "
                              << uncle->data << " → BLACK, grandparent "
                              << grandparent->data << " → RED\n";
                    z->parent->color   = BLACK;
                    uncle->color       = BLACK;
                    grandparent->color = RED;
                    z = grandparent;
                } else {
                    if (z == z->parent->left) {
                        // Case 2 mirror
                        z = z->parent;
                        rightRotate(z);
                    }
                    // Case 3 mirror
                    std::cout << "    [fix] Case 3 recolor (mirror): parent "
                              << z->parent->data << " → BLACK, grandparent "
                              << grandparent->data << " → RED\n";
                    z->parent->color   = BLACK;
                    grandparent->color = RED;
                    leftRotate(grandparent);
                }
            }
        }
        root->color = BLACK;  // root is always black
    }

    // ── HELPERS ─────────────────────────────────────────────
    void inorderHelper(Node* node) {
        if (node == NIL) return;
        inorderHelper(node->left);
        std::cout << node->data
                  << "(" << (node->color == RED ? "R" : "B") << ") ";
        inorderHelper(node->right);
    }

    bool checkBlackHeight(Node* node, int current, int& expected) {
        if (node == NIL) {
            if (expected == 0)
                expected = current;
            return current == expected;
        }
        int add = (node->color == BLACK) ? 1 : 0;
        return checkBlackHeight(node->left,  current + add, expected) &&
               checkBlackHeight(node->right, current + add, expected);
    }

    bool checkNoConsecRed(Node* node) {
        if (node == NIL) return true;
        if (node->color == RED) {
            if (node->left  != NIL && node->left->color  == RED) return false;
            if (node->right != NIL && node->right->color == RED) return false;
        }
        return checkNoConsecRed(node->left) && checkNoConsecRed(node->right);
    }

    void printHelper(Node* node, std::string indent, bool isRight) {
        if (node == NIL) return;
        std::cout << indent;
        if (isRight) {
            std::cout << "R----";
            indent += "     ";
        } else {
            std::cout << "L----";
            indent += "|    ";
        }
        std::string color = (node->color == RED ? "RED" : "BLACK");
        std::cout << node->data << "(" << color << ")\n";
        printHelper(node->left,  indent, false);
        printHelper(node->right, indent, true);
    }
};

// ── MAIN ────────────────────────────────────────────────────
int main() {
    std::cout << "============================================\n"
              << "   Red-Black Tree Implementation\n"
              << "============================================\n\n";

    RedBlackTree rbt;

    // Task 1: tree is initialized empty
    std::cout << ">>> Task 1: Tree Initialized (empty)\n\n";

    // Task 2 & 3: insertions + balancing ops printed automatically
    std::cout << ">>> Task 2 & 3: Insertions + Balancing\n\n";
    int values[] = {10, 20, 30, 15, 25, 5, 1, 7};
    for (int v : values) {
        std::cout << "Inserting " << v << ":\n";
        rbt.insert(v);
        std::cout << "\n";
    }

    // Task 4: search
    std::cout << ">>> Task 4: Search Operations\n\n";
    for (int v : {15, 25, 99, 1}) {
        rbt.search(v);
    }

    // Task 5: inorder traversal
    std::cout << "\n>>> Task 5: Inorder Traversal\n\n";
    rbt.inorder();

    // print tree structure
    std::cout << "\n>>> Tree Structure\n";
    rbt.printTree();

    // Task 6: verify properties
    std::cout << "\n>>> Task 6: Property Verification\n";
    rbt.verifyProperties();

    std::cout << "\n============================================\n"
              << "   Done\n"
              << "============================================\n";
    return 0;
}