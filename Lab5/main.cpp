// Lab 5 - 24BCS10079 - Piyush Bansal
// Red-Black Tree implementation in C++17.
// Supports insert, search, delete, and in-order / level-order traversal.
// I also wrote a small validator that checks the 5 red-black invariants
// after every operation so I can be sure rotations / recoloring are right.

#include <algorithm>
#include <cassert>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

enum Color { RED, BLACK };

struct Node {
    int key;
    Color color;
    Node* parent;
    Node* left;
    Node* right;

    Node(int k, Color c, Node* nilNode)
        : key(k), color(c), parent(nilNode), left(nilNode), right(nilNode) {}
};

class RedBlackTree {
public:
    RedBlackTree() {
        nil_ = new Node(0, BLACK, nullptr);
        nil_->parent = nil_;
        nil_->left   = nil_;
        nil_->right  = nil_;
        root_ = nil_;
    }

    ~RedBlackTree() {
        destroy(root_);
        delete nil_;
    }

    RedBlackTree(const RedBlackTree&)            = delete;
    RedBlackTree& operator=(const RedBlackTree&) = delete;

    void insert(int key) {
        Node* z = new Node(key, RED, nil_);

        Node* y = nil_;
        Node* x = root_;
        while (x != nil_) {
            y = x;
            if (z->key < x->key) x = x->left;
            else                 x = x->right;
        }
        z->parent = y;
        if (y == nil_)              root_   = z;
        else if (z->key < y->key)   y->left  = z;
        else                        y->right = z;

        insertFixup(z);
    }

    bool contains(int key) const {
        return findNode(key) != nil_;
    }

    bool remove(int key) {
        Node* z = findNode(key);
        if (z == nil_) return false;

        Node* y = z;
        Color yOriginalColor = y->color;
        Node* x;

        if (z->left == nil_) {
            x = z->right;
            transplant(z, z->right);
        } else if (z->right == nil_) {
            x = z->left;
            transplant(z, z->left);
        } else {
            y = minimum(z->right);
            yOriginalColor = y->color;
            x = y->right;
            if (y->parent == z) {
                x->parent = y;
            } else {
                transplant(y, y->right);
                y->right = z->right;
                y->right->parent = y;
            }
            transplant(z, y);
            y->left = z->left;
            y->left->parent = y;
            y->color = z->color;
        }

        delete z;
        if (yOriginalColor == BLACK) deleteFixup(x);
        return true;
    }

    // In-order traversal returns keys in sorted order.
    std::vector<int> inorder() const {
        std::vector<int> out;
        inorderHelper(root_, out);
        return out;
    }

    // Level-order print shows the colors so I can visually verify the tree.
    void printLevelOrder(std::ostream& os) const {
        if (root_ == nil_) {
            os << "<empty>\n";
            return;
        }
        std::queue<std::pair<Node*, int>> q;
        q.push({root_, 0});
        int currentLevel = 0;
        os << "L0: ";
        while (!q.empty()) {
            auto [node, level] = q.front();
            q.pop();
            if (level != currentLevel) {
                os << "\nL" << level << ": ";
                currentLevel = level;
            }
            os << node->key << (node->color == RED ? "(R) " : "(B) ");
            if (node->left  != nil_) q.push({node->left,  level + 1});
            if (node->right != nil_) q.push({node->right, level + 1});
        }
        os << "\n";
    }

    // Validate all 5 red-black properties. Returns true if the tree is valid.
    bool validate() const {
        if (root_ == nil_) return true;
        if (root_->color != BLACK) return false;             // property 2
        if (nil_->color  != BLACK) return false;             // property 3
        int blackHeight = 0;
        return validateHelper(root_, 0, blackHeight);
    }

private:
    Node* root_;
    Node* nil_;

    void destroy(Node* x) {
        if (x == nil_ || x == nullptr) return;
        destroy(x->left);
        destroy(x->right);
        delete x;
    }

    Node* findNode(int key) const {
        Node* cur = root_;
        while (cur != nil_ && cur->key != key) {
            cur = (key < cur->key) ? cur->left : cur->right;
        }
        return cur;
    }

    Node* minimum(Node* x) const {
        while (x->left != nil_) x = x->left;
        return x;
    }

    void leftRotate(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        if (y->left != nil_) y->left->parent = x;
        y->parent = x->parent;
        if (x->parent == nil_)            root_ = y;
        else if (x == x->parent->left)    x->parent->left  = y;
        else                              x->parent->right = y;
        y->left = x;
        x->parent = y;
    }

    void rightRotate(Node* x) {
        Node* y = x->left;
        x->left = y->right;
        if (y->right != nil_) y->right->parent = x;
        y->parent = x->parent;
        if (x->parent == nil_)            root_ = y;
        else if (x == x->parent->right)   x->parent->right = y;
        else                              x->parent->left  = y;
        y->right = x;
        x->parent = y;
    }

    void insertFixup(Node* z) {
        while (z->parent->color == RED) {
            if (z->parent == z->parent->parent->left) {
                Node* uncle = z->parent->parent->right;
                if (uncle->color == RED) {
                    // Case 1: uncle is red — recolor and move up.
                    z->parent->color         = BLACK;
                    uncle->color             = BLACK;
                    z->parent->parent->color = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->right) {
                        // Case 2: turn into case 3 with a left rotation.
                        z = z->parent;
                        leftRotate(z);
                    }
                    // Case 3: recolor + right rotate the grandparent.
                    z->parent->color         = BLACK;
                    z->parent->parent->color = RED;
                    rightRotate(z->parent->parent);
                }
            } else {
                // Mirror of the above.
                Node* uncle = z->parent->parent->left;
                if (uncle->color == RED) {
                    z->parent->color         = BLACK;
                    uncle->color             = BLACK;
                    z->parent->parent->color = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        rightRotate(z);
                    }
                    z->parent->color         = BLACK;
                    z->parent->parent->color = RED;
                    leftRotate(z->parent->parent);
                }
            }
        }
        root_->color = BLACK;
    }

    void transplant(Node* u, Node* v) {
        if (u->parent == nil_)             root_ = v;
        else if (u == u->parent->left)     u->parent->left  = v;
        else                               u->parent->right = v;
        v->parent = u->parent;
    }

    void deleteFixup(Node* x) {
        while (x != root_ && x->color == BLACK) {
            if (x == x->parent->left) {
                Node* w = x->parent->right;
                if (w->color == RED) {
                    // Case 1: sibling is red — rotate it down.
                    w->color         = BLACK;
                    x->parent->color = RED;
                    leftRotate(x->parent);
                    w = x->parent->right;
                }
                if (w->left->color == BLACK && w->right->color == BLACK) {
                    // Case 2: both of sibling's children are black — recolor and move up.
                    w->color = RED;
                    x = x->parent;
                } else {
                    if (w->right->color == BLACK) {
                        // Case 3: sibling's right child is black — rotate sibling to set up case 4.
                        w->left->color = BLACK;
                        w->color       = RED;
                        rightRotate(w);
                        w = x->parent->right;
                    }
                    // Case 4: sibling's right child is red — fix and exit.
                    w->color         = x->parent->color;
                    x->parent->color = BLACK;
                    w->right->color  = BLACK;
                    leftRotate(x->parent);
                    x = root_;
                }
            } else {
                // Mirror of the above.
                Node* w = x->parent->left;
                if (w->color == RED) {
                    w->color         = BLACK;
                    x->parent->color = RED;
                    rightRotate(x->parent);
                    w = x->parent->left;
                }
                if (w->right->color == BLACK && w->left->color == BLACK) {
                    w->color = RED;
                    x = x->parent;
                } else {
                    if (w->left->color == BLACK) {
                        w->right->color = BLACK;
                        w->color        = RED;
                        leftRotate(w);
                        w = x->parent->left;
                    }
                    w->color         = x->parent->color;
                    x->parent->color = BLACK;
                    w->left->color   = BLACK;
                    rightRotate(x->parent);
                    x = root_;
                }
            }
        }
        x->color = BLACK;
    }

    void inorderHelper(Node* x, std::vector<int>& out) const {
        if (x == nil_) return;
        inorderHelper(x->left, out);
        out.push_back(x->key);
        inorderHelper(x->right, out);
    }

    bool validateHelper(Node* x, int blackCount, int& pathBlackHeight) const {
        if (x == nil_) {
            blackCount += 1; // count the nil sentinel as black
            if (pathBlackHeight == 0) {
                pathBlackHeight = blackCount;
                return true;
            }
            return blackCount == pathBlackHeight;
        }
        // Property 4: a red node cannot have a red child.
        if (x->color == RED) {
            if (x->left->color == RED || x->right->color == RED) return false;
        }
        int countForThisNode = blackCount + (x->color == BLACK ? 1 : 0);
        return validateHelper(x->left,  countForThisNode, pathBlackHeight)
            && validateHelper(x->right, countForThisNode, pathBlackHeight);
    }
};

// ---------- demo ----------

static void printState(const std::string& label, RedBlackTree& t) {
    std::cout << "\n=== " << label << " ===\n";
    t.printLevelOrder(std::cout);
    auto v = t.inorder();
    std::cout << "inorder: ";
    for (int k : v) std::cout << k << ' ';
    std::cout << "\nvalid:   " << (t.validate() ? "yes" : "no") << "\n";
    assert(t.validate());
}

int main() {
    RedBlackTree tree;

    // Insert sequence that exercises every case of insertFixup.
    std::vector<int> keys = {30, 20, 40, 10, 25, 35, 50, 5, 15, 27, 45, 60};
    for (int k : keys) tree.insert(k);
    printState("after inserts", tree);

    std::cout << "\ncontains(25) -> " << (tree.contains(25) ? "yes" : "no") << "\n";
    std::cout << "contains(99) -> " << (tree.contains(99) ? "yes" : "no") << "\n";

    // Delete a leaf, an internal node with one child, and the root.
    tree.remove(5);
    printState("after remove(5)  - red leaf", tree);

    tree.remove(40);
    printState("after remove(40) - internal w/ children", tree);

    tree.remove(30);
    printState("after remove(30) - the root", tree);

    // Bulk delete the rest to make sure the tree stays valid until empty.
    for (int k : {10, 15, 20, 25, 27, 35, 45, 50, 60}) {
        tree.remove(k);
        if (!tree.validate()) {
            std::cout << "INVARIANT BROKEN after removing " << k << "\n";
            return 1;
        }
    }
    printState("after deleting everything", tree);

    return 0;
}
