// Lab 4, Part 1: Red-Black Tree
// Aditya Bhaskara (24BCS10058)
//
// A self balancing binary search tree. It keeps height at O(log n) by colouring
// nodes red or black and repairing a handful of local invariants after every
// insert and delete. This is the structure behind std::map and std::set, and it
// is the in-memory cousin of the on-disk B-Tree in part 2.
//
// Invariants:
//   1. Every node is red or black.
//   2. The root is black.
//   3. A red node never has a red child.
//   4. Every root-to-leaf path crosses the same number of black nodes.
//
// We use a single shared "nil" sentinel for all leaves instead of nullptr. That
// removes a pile of null checks and makes the delete fixup readable, which is
// the part most implementations get wrong.
//
// Build: g++ -std=c++17 -o red_black_tree red_black_tree.cpp
// Run:   ./red_black_tree

#include <initializer_list>
#include <iostream>

enum class Color { Red, Black };

struct Node {
    int    key;
    Color  color;
    Node*  left;
    Node*  right;
    Node*  parent;
};

class RedBlackTree {
public:
    RedBlackTree() {
        nil_ = new Node{0, Color::Black, nullptr, nullptr, nullptr};
        root_ = nil_;
    }

    ~RedBlackTree() {
        destroy(root_);
        delete nil_;
    }

    // The tree owns raw nodes, so copying it would shallow copy the pointers and
    // double free them. Disable copy and move; callers pass the tree by reference.
    RedBlackTree(const RedBlackTree&) = delete;
    RedBlackTree& operator=(const RedBlackTree&) = delete;
    RedBlackTree(RedBlackTree&&) = delete;
    RedBlackTree& operator=(RedBlackTree&&) = delete;

    void insert(int key) {
        Node* node = new Node{key, Color::Red, nil_, nil_, nil_};

        Node* parent = nil_;
        Node* cur    = root_;
        while (cur != nil_) {
            parent = cur;
            cur = (key < cur->key) ? cur->left : cur->right;
        }

        node->parent = parent;
        if (parent == nil_)            root_ = node;
        else if (key < parent->key)    parent->left = node;
        else                           parent->right = node;

        insert_fixup(node);
    }

    void remove(int key) {
        Node* z = find(key);
        if (z == nil_) return;

        Node* y = z;                       // node actually unlinked from the tree
        Node* x = nil_;                    // node that moves into y's old spot
        Color y_original = y->color;

        if (z->left == nil_) {
            x = z->right;
            transplant(z, z->right);
        } else if (z->right == nil_) {
            x = z->left;
            transplant(z, z->left);
        } else {
            y = minimum(z->right);         // in-order successor
            y_original = y->color;
            x = y->right;
            if (y->parent == z) {
                x->parent = y;             // valid even when x is the sentinel
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
        if (y_original == Color::Black) delete_fixup(x);
    }

    bool contains(int key) const { return find(key) != nil_; }

    void print() const {
        inorder(root_);
        std::cout << "\n";
    }

    // Confirms the four invariants hold. Returns the black height when valid,
    // or -1 when the tree is broken. Handy as a self check after each mutation.
    int validate() const {
        if (root_->color != Color::Black) return -1;
        return black_height(root_);
    }

private:
    void destroy(Node* n) {
        if (n == nil_) return;
        destroy(n->left);
        destroy(n->right);
        delete n;
    }

    Node* find(int key) const {
        Node* cur = root_;
        while (cur != nil_ && cur->key != key)
            cur = (key < cur->key) ? cur->left : cur->right;
        return cur;
    }

    Node* minimum(Node* n) const {
        while (n->left != nil_) n = n->left;
        return n;
    }

    void left_rotate(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        if (y->left != nil_) y->left->parent = x;
        y->parent = x->parent;
        if (x->parent == nil_)        root_ = y;
        else if (x == x->parent->left) x->parent->left = y;
        else                           x->parent->right = y;
        y->left = x;
        x->parent = y;
    }

    void right_rotate(Node* x) {
        Node* y = x->left;
        x->left = y->right;
        if (y->right != nil_) y->right->parent = x;
        y->parent = x->parent;
        if (x->parent == nil_)         root_ = y;
        else if (x == x->parent->right) x->parent->right = y;
        else                            x->parent->left = y;
        y->right = x;
        x->parent = y;
    }

    void insert_fixup(Node* z) {
        while (z->parent->color == Color::Red) {
            Node* grandparent = z->parent->parent;
            if (z->parent == grandparent->left) {
                Node* uncle = grandparent->right;
                if (uncle->color == Color::Red) {
                    // Case 1: uncle is red, push blackness down and recurse up.
                    z->parent->color = Color::Black;
                    uncle->color     = Color::Black;
                    grandparent->color = Color::Red;
                    z = grandparent;
                } else {
                    if (z == z->parent->right) {
                        // Case 2: turn a "zig-zag" into a straight line.
                        z = z->parent;
                        left_rotate(z);
                    }
                    // Case 3: straight line, recolor and rotate the grandparent.
                    z->parent->color = Color::Black;
                    grandparent->color = Color::Red;
                    right_rotate(grandparent);
                }
            } else {
                // Mirror image of the above.
                Node* uncle = grandparent->left;
                if (uncle->color == Color::Red) {
                    z->parent->color = Color::Black;
                    uncle->color     = Color::Black;
                    grandparent->color = Color::Red;
                    z = grandparent;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        right_rotate(z);
                    }
                    z->parent->color = Color::Black;
                    grandparent->color = Color::Red;
                    left_rotate(grandparent);
                }
            }
        }
        root_->color = Color::Black;
    }

    void transplant(Node* u, Node* v) {
        if (u->parent == nil_)        root_ = v;
        else if (u == u->parent->left) u->parent->left = v;
        else                           u->parent->right = v;
        v->parent = u->parent;
    }

    void delete_fixup(Node* x) {
        while (x != root_ && x->color == Color::Black) {
            if (x == x->parent->left) {
                Node* sibling = x->parent->right;
                if (sibling->color == Color::Red) {
                    // Case 1: red sibling, rotate to fall into the cases below.
                    sibling->color   = Color::Black;
                    x->parent->color = Color::Red;
                    left_rotate(x->parent);
                    sibling = x->parent->right;
                }
                if (sibling->left->color == Color::Black &&
                    sibling->right->color == Color::Black) {
                    // Case 2: both nephews black, recolor and move the deficit up.
                    sibling->color = Color::Red;
                    x = x->parent;
                } else {
                    if (sibling->right->color == Color::Black) {
                        // Case 3: rotate so the far nephew becomes red.
                        sibling->left->color = Color::Black;
                        sibling->color = Color::Red;
                        right_rotate(sibling);
                        sibling = x->parent->right;
                    }
                    // Case 4: far nephew is red, rotate the deficit away for good.
                    sibling->color = x->parent->color;
                    x->parent->color = Color::Black;
                    sibling->right->color = Color::Black;
                    left_rotate(x->parent);
                    x = root_;
                }
            } else {
                // Mirror image of the above.
                Node* sibling = x->parent->left;
                if (sibling->color == Color::Red) {
                    sibling->color   = Color::Black;
                    x->parent->color = Color::Red;
                    right_rotate(x->parent);
                    sibling = x->parent->left;
                }
                if (sibling->right->color == Color::Black &&
                    sibling->left->color == Color::Black) {
                    sibling->color = Color::Red;
                    x = x->parent;
                } else {
                    if (sibling->left->color == Color::Black) {
                        sibling->right->color = Color::Black;
                        sibling->color = Color::Red;
                        left_rotate(sibling);
                        sibling = x->parent->left;
                    }
                    sibling->color = x->parent->color;
                    x->parent->color = Color::Black;
                    sibling->left->color = Color::Black;
                    right_rotate(x->parent);
                    x = root_;
                }
            }
        }
        x->color = Color::Black;
    }

    void inorder(Node* n) const {
        if (n == nil_) return;
        inorder(n->left);
        std::cout << n->key << (n->color == Color::Red ? "(R) " : "(B) ");
        inorder(n->right);
    }

    // Returns the black height of the subtree, or -1 if any invariant is broken.
    int black_height(Node* n) const {
        if (n == nil_) return 1;                       // sentinel counts as black
        if (n->color == Color::Red &&
            (n->left->color == Color::Red || n->right->color == Color::Red))
            return -1;                                 // red node with a red child
        int left  = black_height(n->left);
        int right = black_height(n->right);
        if (left == -1 || right == -1 || left != right) return -1;
        return left + (n->color == Color::Black ? 1 : 0);
    }

    Node* nil_;
    Node* root_;
};

int main() {
    RedBlackTree tree;

    std::cout << "inserting: 10 20 30 15 25 5 1 40 35 50\n";
    for (int key : {10, 20, 30, 15, 25, 5, 1, 40, 35, 50})
        tree.insert(key);

    std::cout << "inorder (key + color): ";
    tree.print();
    int black_height = tree.validate();
    std::cout << "valid? " << (black_height != -1 ? "yes" : "no")
              << "  (black height = " << black_height << ")\n\n";

    for (int key : {20, 10, 35}) {
        tree.remove(key);
        std::cout << "after removing " << key << ": ";
        tree.print();
        std::cout << "valid? " << (tree.validate() != -1 ? "yes" : "no") << "\n";
    }

    std::cout << "\ncontains(25)? " << (tree.contains(25) ? "yes" : "no") << "\n";
    std::cout << "contains(10)? " << (tree.contains(10) ? "yes" : "no") << "\n";
    return 0;
}
