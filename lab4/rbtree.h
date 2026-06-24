#pragma once
/**
 * Lab 4 — Red-Black Tree Implementation
 *
 * A self-balancing BST where every node is either RED or BLACK.
 * Properties:
 *   1. Every node is RED or BLACK
 *   2. Root is always BLACK
 *   3. Every leaf (NIL) is BLACK
 *   4. If a node is RED, both children are BLACK (no red-red)
 *   5. All paths from root to leaves have the same black-height
 */

#include <iostream>
#include <functional>
#include <string>
#include <queue>
#include <iomanip>

enum class Color { RED, BLACK };

template <typename K>
struct RBNode {
    K       key;
    Color   color;
    RBNode* left;
    RBNode* right;
    RBNode* parent;

    RBNode(K k, Color c, RBNode* nil)
        : key(k), color(c), left(nil), right(nil), parent(nil) {}
};

template <typename K>
class RedBlackTree {
private:
    using Node = RBNode<K>;
    Node* root_;
    Node* NIL_;   // sentinel node (represents all leaves)
    int   size_;

    // ─── Rotations ───
    void rotate_left(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        if (y->left != NIL_) y->left->parent = x;
        y->parent = x->parent;
        if (x->parent == NIL_)       root_ = y;
        else if (x == x->parent->left)  x->parent->left = y;
        else                             x->parent->right = y;
        y->left = x;
        x->parent = y;
    }

    void rotate_right(Node* x) {
        Node* y = x->left;
        x->left = y->right;
        if (y->right != NIL_) y->right->parent = x;
        y->parent = x->parent;
        if (x->parent == NIL_)       root_ = y;
        else if (x == x->parent->right) x->parent->right = y;
        else                             x->parent->left = y;
        y->right = x;
        x->parent = y;
    }

    // ─── Insert Fixup ───
    void insert_fixup(Node* z) {
        while (z->parent->color == Color::RED) {
            if (z->parent == z->parent->parent->left) {
                Node* uncle = z->parent->parent->right;
                if (uncle->color == Color::RED) {
                    // Case 1: Uncle is RED → recolor
                    z->parent->color = Color::BLACK;
                    uncle->color = Color::BLACK;
                    z->parent->parent->color = Color::RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->right) {
                        // Case 2: z is right child → left rotate parent
                        z = z->parent;
                        rotate_left(z);
                    }
                    // Case 3: z is left child → right rotate grandparent
                    z->parent->color = Color::BLACK;
                    z->parent->parent->color = Color::RED;
                    rotate_right(z->parent->parent);
                }
            } else {
                // Mirror cases (parent is right child)
                Node* uncle = z->parent->parent->left;
                if (uncle->color == Color::RED) {
                    z->parent->color = Color::BLACK;
                    uncle->color = Color::BLACK;
                    z->parent->parent->color = Color::RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        rotate_right(z);
                    }
                    z->parent->color = Color::BLACK;
                    z->parent->parent->color = Color::RED;
                    rotate_left(z->parent->parent);
                }
            }
        }
        root_->color = Color::BLACK;
    }

    // ─── Transplant (replace subtree u with subtree v) ───
    void transplant(Node* u, Node* v) {
        if (u->parent == NIL_)       root_ = v;
        else if (u == u->parent->left)  u->parent->left = v;
        else                             u->parent->right = v;
        v->parent = u->parent;
    }

    Node* minimum(Node* x) {
        while (x->left != NIL_) x = x->left;
        return x;
    }

    // ─── Delete Fixup ───
    void delete_fixup(Node* x) {
        while (x != root_ && x->color == Color::BLACK) {
            if (x == x->parent->left) {
                Node* w = x->parent->right;
                if (w->color == Color::RED) {
                    // Case 1: sibling is red
                    w->color = Color::BLACK;
                    x->parent->color = Color::RED;
                    rotate_left(x->parent);
                    w = x->parent->right;
                }
                if (w->left->color == Color::BLACK && w->right->color == Color::BLACK) {
                    // Case 2: both of sibling's children are black
                    w->color = Color::RED;
                    x = x->parent;
                } else {
                    if (w->right->color == Color::BLACK) {
                        // Case 3: sibling's right child is black
                        w->left->color = Color::BLACK;
                        w->color = Color::RED;
                        rotate_right(w);
                        w = x->parent->right;
                    }
                    // Case 4: sibling's right child is red
                    w->color = x->parent->color;
                    x->parent->color = Color::BLACK;
                    w->right->color = Color::BLACK;
                    rotate_left(x->parent);
                    x = root_;
                }
            } else {
                // Mirror cases
                Node* w = x->parent->left;
                if (w->color == Color::RED) {
                    w->color = Color::BLACK;
                    x->parent->color = Color::RED;
                    rotate_right(x->parent);
                    w = x->parent->left;
                }
                if (w->right->color == Color::BLACK && w->left->color == Color::BLACK) {
                    w->color = Color::RED;
                    x = x->parent;
                } else {
                    if (w->left->color == Color::BLACK) {
                        w->right->color = Color::BLACK;
                        w->color = Color::RED;
                        rotate_left(w);
                        w = x->parent->left;
                    }
                    w->color = x->parent->color;
                    x->parent->color = Color::BLACK;
                    w->left->color = Color::BLACK;
                    rotate_right(x->parent);
                    x = root_;
                }
            }
        }
        x->color = Color::BLACK;
    }

    // ─── Recursive cleanup ───
    void destroy(Node* node) {
        if (node != NIL_) {
            destroy(node->left);
            destroy(node->right);
            delete node;
        }
    }

    // ─── Inorder traversal ───
    void inorder_impl(Node* node, std::function<void(K, Color)> visit) const {
        if (node != NIL_) {
            inorder_impl(node->left, visit);
            visit(node->key, node->color);
            inorder_impl(node->right, visit);
        }
    }

    // ─── Verify RB properties ───
    int verify_impl(Node* node) const {
        if (node == NIL_) return 1;  // NIL nodes have black-height 1

        // Property 4: No red-red
        if (node->color == Color::RED) {
            if (node->left->color == Color::RED || node->right->color == Color::RED) {
                std::cerr << "VIOLATION: Red node " << node->key << " has red child!" << std::endl;
                return -1;
            }
        }

        int left_bh = verify_impl(node->left);
        int right_bh = verify_impl(node->right);

        if (left_bh == -1 || right_bh == -1) return -1;

        // Property 5: Equal black-height
        if (left_bh != right_bh) {
            std::cerr << "VIOLATION: Unequal black-height at node " << node->key
                      << " (left=" << left_bh << ", right=" << right_bh << ")" << std::endl;
            return -1;
        }

        return left_bh + (node->color == Color::BLACK ? 1 : 0);
    }

public:
    RedBlackTree() : size_(0) {
        NIL_ = new Node(K{}, Color::BLACK, nullptr);
        NIL_->left = NIL_;
        NIL_->right = NIL_;
        NIL_->parent = NIL_;
        root_ = NIL_;
    }

    ~RedBlackTree() {
        destroy(root_);
        delete NIL_;
    }

    // ─── Insert ───
    void insert(K key) {
        Node* z = new Node(key, Color::RED, NIL_);
        Node* y = NIL_;
        Node* x = root_;

        // BST insert
        while (x != NIL_) {
            y = x;
            if (key < x->key)      x = x->left;
            else if (key > x->key) x = x->right;
            else {
                delete z;  // duplicate
                return;
            }
        }
        z->parent = y;
        if (y == NIL_)          root_ = z;
        else if (key < y->key)  y->left = z;
        else                     y->right = z;

        size_++;
        insert_fixup(z);
    }

    // ─── Delete ───
    bool remove(K key) {
        Node* z = root_;
        while (z != NIL_) {
            if (key < z->key)       z = z->left;
            else if (key > z->key)  z = z->right;
            else break;
        }
        if (z == NIL_) return false;  // not found

        Node* y = z;
        Color y_original_color = y->color;
        Node* x;

        if (z->left == NIL_) {
            x = z->right;
            transplant(z, z->right);
        } else if (z->right == NIL_) {
            x = z->left;
            transplant(z, z->left);
        } else {
            y = minimum(z->right);
            y_original_color = y->color;
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
        size_--;

        if (y_original_color == Color::BLACK) {
            delete_fixup(x);
        }
        return true;
    }

    // ─── Search ───
    bool search(K key) const {
        Node* x = root_;
        while (x != NIL_) {
            if (key < x->key)       x = x->left;
            else if (key > x->key)  x = x->right;
            else return true;
        }
        return false;
    }

    // ─── Traversal ───
    void inorder(std::function<void(K, Color)> visit) const {
        inorder_impl(root_, visit);
    }

    // ─── Verify RB Properties ───
    bool verify() const {
        if (root_ == NIL_) return true;

        // Property 2: Root is black
        if (root_->color != Color::RED && root_->color != Color::BLACK) {
            std::cerr << "VIOLATION: Root color invalid!" << std::endl;
            return false;
        }
        if (root_->color == Color::RED) {
            std::cerr << "VIOLATION: Root is RED!" << std::endl;
            return false;
        }

        return verify_impl(root_) != -1;
    }

    // ─── Print tree structure ───
    void print() const {
        if (root_ == NIL_) {
            std::cout << "  (empty tree)" << std::endl;
            return;
        }

        // BFS level-order print
        std::queue<std::pair<Node*, int>> q;
        q.push({root_, 0});
        int prev_level = -1;

        while (!q.empty()) {
            auto [node, level] = q.front(); q.pop();
            if (level != prev_level) {
                if (prev_level >= 0) std::cout << std::endl;
                std::cout << "  Level " << level << ": ";
                prev_level = level;
            }

            std::string color_str = (node->color == Color::RED) ? "R" : "B";
            std::cout << node->key << "(" << color_str << ") ";

            if (node->left != NIL_) q.push({node->left, level + 1});
            if (node->right != NIL_) q.push({node->right, level + 1});
        }
        std::cout << std::endl;
    }

    int size() const { return size_; }
    bool empty() const { return root_ == NIL_; }
};
