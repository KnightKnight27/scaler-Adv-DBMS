// Lab 5 - Red-Black Tree
// Author: 24BCS10345 Ansh Mahajan
//
// A self-balancing binary search tree. Every node is RED or BLACK and the
// following invariants are kept after every insert, which together bound the
// height at 2*log2(n+1):
//
//   1. Every node is either red or black.
//   2. The root is black.
//   3. A red node never has a red child (no two reds in a row).
//   4. Every root-to-NIL path crosses the same number of black nodes
//      (the "black height").
//
// A single shared sentinel node represents every NIL leaf; it is black, which
// keeps the rebalancing cases free of null checks.
//
// API:
//   RedBlackTree<T>
//     void insert(const T& key)        - insert, then restore the invariants
//     bool contains(const T& key)      - BST lookup
//     void inorder()                   - sorted traversal (key + colour)
//     void printTree()                 - sideways structural view
//     bool validate()                  - assert all four properties hold
//
// Build: g++ -std=c++17 red_black_tree.cpp -o rbt
// Run:   ./rbt

#include <iostream>
#include <string>
#include <vector>

template <typename T>
class RedBlackTree {
public:
    RedBlackTree() {
        nil_ = new Node(T{}, Colour::Black);   // shared black sentinel
        nil_->left = nil_->right = nil_->parent = nil_;
        root_ = nil_;
    }

    ~RedBlackTree() {
        destroy(root_);
        delete nil_;
    }

    RedBlackTree(const RedBlackTree&) = delete;
    RedBlackTree& operator=(const RedBlackTree&) = delete;

    // Standard BST insert followed by the red-black fix-up walk.
    void insert(const T& key) {
        Node* node = new Node(key, Colour::Red);  // new nodes start red
        node->left = node->right = nil_;

        Node* parent = nil_;
        Node* cursor = root_;
        while (cursor != nil_) {
            parent = cursor;
            if (key < cursor->key) {
                cursor = cursor->left;
            } else if (cursor->key < key) {
                cursor = cursor->right;
            } else {
                delete node;                       // duplicate: ignore
                return;
            }
        }

        node->parent = parent;
        if (parent == nil_) {
            root_ = node;                          // tree was empty
        } else if (key < parent->key) {
            parent->left = node;
        } else {
            parent->right = node;
        }

        insertFixup(node);
    }

    bool contains(const T& key) const {
        Node* cursor = root_;
        while (cursor != nil_) {
            if (key < cursor->key) {
                cursor = cursor->left;
            } else if (cursor->key < key) {
                cursor = cursor->right;
            } else {
                return true;
            }
        }
        return false;
    }

    void inorder() const {
        bool first = true;
        inorder(root_, first);
        std::cout << '\n';
    }

    // Rotated 90 degrees: read it with your head tilted left. Right subtree
    // prints above the node, left subtree below.
    void printTree() const {
        if (root_ == nil_) {
            std::cout << "(empty tree)\n";
            return;
        }
        printTree(root_, 0);
    }

    // Re-checks every invariant from scratch - used by the test harness so we
    // never just trust that the fix-up was correct.
    bool validate() const {
        if (root_->colour != Colour::Black) {
            std::cout << "  [FAIL] root is not black\n";
            return false;
        }
        if (!noRedHasRedChild(root_)) {
            std::cout << "  [FAIL] a red node has a red child\n";
            return false;
        }
        if (blackHeight(root_) == kBadHeight) {
            std::cout << "  [FAIL] black height differs between paths\n";
            return false;
        }
        return true;
    }

private:
    enum class Colour { Red, Black };

    struct Node {
        T key;
        Colour colour;
        Node* left;
        Node* right;
        Node* parent;
        Node(const T& k, Colour c)
            : key(k), colour(c), left(nullptr), right(nullptr), parent(nullptr) {}
    };

    static constexpr int kBadHeight = -1;

    Node* root_;
    Node* nil_;   // sentinel; always black

    //                x                  y
    //              (a) (y)    =>      (x) (c)
    //                  (b)(c)      (a)(b)
    void rotateLeft(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        if (y->left != nil_) {
            y->left->parent = x;
        }
        y->parent = x->parent;
        if (x->parent == nil_) {
            root_ = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }
        y->left = x;
        x->parent = y;
    }

    // Mirror image of rotateLeft.
    void rotateRight(Node* x) {
        Node* y = x->left;
        x->left = y->right;
        if (y->right != nil_) {
            y->right->parent = x;
        }
        y->parent = x->parent;
        if (x->parent == nil_) {
            root_ = y;
        } else if (x == x->parent->right) {
            x->parent->right = y;
        } else {
            x->parent->left = y;
        }
        y->right = x;
        x->parent = y;
    }

    // Restore the invariants after inserting a red node. The only invariant a
    // red insert can break is "no red-red"; we push that violation up the tree
    // until it disappears or reaches the root (which we then paint black).
    void insertFixup(Node* node) {
        while (node->parent->colour == Colour::Red) {
            Node* grandparent = node->parent->parent;
            if (node->parent == grandparent->left) {
                Node* uncle = grandparent->right;
                if (uncle->colour == Colour::Red) {
                    // Case 1: red uncle -> recolour and move up two levels.
                    node->parent->colour = Colour::Black;
                    uncle->colour = Colour::Black;
                    grandparent->colour = Colour::Red;
                    node = grandparent;
                } else {
                    if (node == node->parent->right) {
                        // Case 2: bend the "triangle" into a "line".
                        node = node->parent;
                        rotateLeft(node);
                    }
                    // Case 3: recolour and rotate the grandparent down.
                    node->parent->colour = Colour::Black;
                    grandparent->colour = Colour::Red;
                    rotateRight(grandparent);
                }
            } else {
                // Symmetric: parent is the right child of the grandparent.
                Node* uncle = grandparent->left;
                if (uncle->colour == Colour::Red) {
                    node->parent->colour = Colour::Black;
                    uncle->colour = Colour::Black;
                    grandparent->colour = Colour::Red;
                    node = grandparent;
                } else {
                    if (node == node->parent->left) {
                        node = node->parent;
                        rotateRight(node);
                    }
                    node->parent->colour = Colour::Black;
                    grandparent->colour = Colour::Red;
                    rotateLeft(grandparent);
                }
            }
        }
        root_->colour = Colour::Black;   // invariant 2, restored cheaply
    }

    void inorder(Node* node, bool& first) const {
        if (node == nil_) {
            return;
        }
        inorder(node->left, first);
        if (!first) {
            std::cout << ", ";
        }
        first = false;
        std::cout << node->key << '(' << colourTag(node->colour) << ')';
        inorder(node->right, first);
    }

    void printTree(Node* node, int depth) const {
        if (node == nil_) {
            return;
        }
        printTree(node->right, depth + 1);
        std::cout << std::string(static_cast<std::size_t>(depth) * 4, ' ')
                  << node->key << '(' << colourTag(node->colour) << ")\n";
        printTree(node->left, depth + 1);
    }

    bool noRedHasRedChild(Node* node) const {
        if (node == nil_) {
            return true;
        }
        if (node->colour == Colour::Red) {
            if (node->left->colour == Colour::Red ||
                node->right->colour == Colour::Red) {
                return false;
            }
        }
        return noRedHasRedChild(node->left) && noRedHasRedChild(node->right);
    }

    // Returns the black height of the subtree, or kBadHeight if the two sides
    // disagree (which means invariant 4 is violated somewhere below).
    int blackHeight(Node* node) const {
        if (node == nil_) {
            return 1;   // the sentinel counts as one black node
        }
        int left = blackHeight(node->left);
        int right = blackHeight(node->right);
        if (left == kBadHeight || right == kBadHeight || left != right) {
            return kBadHeight;
        }
        return left + (node->colour == Colour::Black ? 1 : 0);
    }

    void destroy(Node* node) {
        if (node == nil_) {
            return;
        }
        destroy(node->left);
        destroy(node->right);
        delete node;
    }

    static char colourTag(Colour c) {
        return c == Colour::Red ? 'R' : 'B';
    }
};

int main() {
    RedBlackTree<int> tree;

    // A deliberately unsorted feed so the rebalancing actually has work to do.
    const std::vector<int> keys = {41, 38, 31, 12, 19, 8, 27, 50, 45, 5, 33};

    std::cout << "Inserting keys: ";
    for (std::size_t i = 0; i < keys.size(); ++i) {
        std::cout << keys[i] << (i + 1 < keys.size() ? " " : "\n");
        tree.insert(keys[i]);
    }

    std::cout << "\nInorder (sorted, with R/B colour):\n  ";
    tree.inorder();

    std::cout << "\nTree structure (rotate 90 clockwise to read):\n";
    tree.printTree();

    std::cout << "\nLookups:\n";
    for (int probe : {27, 33, 99}) {
        std::cout << "  contains(" << probe << ") = "
                  << (tree.contains(probe) ? "true" : "false") << '\n';
    }

    std::cout << "\nProperty check after all inserts:\n";
    std::cout << (tree.validate() ? "  [OK] all four red-black properties hold\n"
                                  : "  [FAIL] tree is not a valid red-black tree\n");

    return 0;
}
