#include <iostream>
#include <vector>

class RedBlackTree {
    enum class Color { Red, Black };

    struct Node {
        int key = 0;
        Color color = Color::Black;
        Node* parent = nullptr;
        Node* left = nullptr;
        Node* right = nullptr;
    };

public:
    RedBlackTree() {
        nil_ = new Node{};
        nil_->left = nil_;
        nil_->right = nil_;
        nil_->parent = nil_;
        root_ = nil_;
    }

    ~RedBlackTree() {
        destroy(root_);
        delete nil_;
    }

    void insert(int key) {
        Node* node = new Node{key, Color::Red, nil_, nil_, nil_};
        Node* parent = nil_;
        Node* walk = root_;

        while (walk != nil_) {
            parent = walk;
            walk = key < walk->key ? walk->left : walk->right;
        }

        node->parent = parent;
        if (parent == nil_) {
            root_ = node;
        } else if (key < parent->key) {
            parent->left = node;
        } else {
            parent->right = node;
        }

        restore_after_insert(node);
    }

    bool contains(int key) const {
        return find_node(key) != nil_;
    }

    void erase(int key) {
        Node* victim = find_node(key);
        if (victim == nil_) {
            return;
        }

        Node* moved = victim;
        Color moved_original = moved->color;
        Node* fix_from = nil_;

        if (victim->left == nil_) {
            fix_from = victim->right;
            replace(victim, victim->right);
        } else if (victim->right == nil_) {
            fix_from = victim->left;
            replace(victim, victim->left);
        } else {
            moved = minimum(victim->right);
            moved_original = moved->color;
            fix_from = moved->right;

            if (moved->parent == victim) {
                fix_from->parent = moved;
            } else {
                replace(moved, moved->right);
                moved->right = victim->right;
                moved->right->parent = moved;
            }

            replace(victim, moved);
            moved->left = victim->left;
            moved->left->parent = moved;
            moved->color = victim->color;
        }

        delete victim;
        if (moved_original == Color::Black) {
            restore_after_delete(fix_from);
        }
    }

    void print_sorted() const {
        print_sorted(root_);
        std::cout << '\n';
    }

private:
    Node* nil_ = nullptr;
    Node* root_ = nullptr;

    void destroy(Node* node) {
        if (node == nil_) {
            return;
        }
        destroy(node->left);
        destroy(node->right);
        delete node;
    }

    Node* find_node(int key) const {
        Node* walk = root_;
        while (walk != nil_ && walk->key != key) {
            walk = key < walk->key ? walk->left : walk->right;
        }
        return walk;
    }

    Node* minimum(Node* node) const {
        while (node->left != nil_) {
            node = node->left;
        }
        return node;
    }

    void rotate_left(Node* pivot) {
        Node* child = pivot->right;
        pivot->right = child->left;
        if (child->left != nil_) {
            child->left->parent = pivot;
        }
        child->parent = pivot->parent;
        if (pivot->parent == nil_) {
            root_ = child;
        } else if (pivot == pivot->parent->left) {
            pivot->parent->left = child;
        } else {
            pivot->parent->right = child;
        }
        child->left = pivot;
        pivot->parent = child;
    }

    void rotate_right(Node* pivot) {
        Node* child = pivot->left;
        pivot->left = child->right;
        if (child->right != nil_) {
            child->right->parent = pivot;
        }
        child->parent = pivot->parent;
        if (pivot->parent == nil_) {
            root_ = child;
        } else if (pivot == pivot->parent->right) {
            pivot->parent->right = child;
        } else {
            pivot->parent->left = child;
        }
        child->right = pivot;
        pivot->parent = child;
    }

    void restore_after_insert(Node* node) {
        while (node->parent->color == Color::Red) {
            if (node->parent == node->parent->parent->left) {
                Node* uncle = node->parent->parent->right;
                if (uncle->color == Color::Red) {
                    node->parent->color = Color::Black;
                    uncle->color = Color::Black;
                    node->parent->parent->color = Color::Red;
                    node = node->parent->parent;
                } else {
                    if (node == node->parent->right) {
                        node = node->parent;
                        rotate_left(node);
                    }
                    node->parent->color = Color::Black;
                    node->parent->parent->color = Color::Red;
                    rotate_right(node->parent->parent);
                }
            } else {
                Node* uncle = node->parent->parent->left;
                if (uncle->color == Color::Red) {
                    node->parent->color = Color::Black;
                    uncle->color = Color::Black;
                    node->parent->parent->color = Color::Red;
                    node = node->parent->parent;
                } else {
                    if (node == node->parent->left) {
                        node = node->parent;
                        rotate_right(node);
                    }
                    node->parent->color = Color::Black;
                    node->parent->parent->color = Color::Red;
                    rotate_left(node->parent->parent);
                }
            }
        }
        root_->color = Color::Black;
    }

    void replace(Node* old_node, Node* new_node) {
        if (old_node->parent == nil_) {
            root_ = new_node;
        } else if (old_node == old_node->parent->left) {
            old_node->parent->left = new_node;
        } else {
            old_node->parent->right = new_node;
        }
        new_node->parent = old_node->parent;
    }

    void restore_after_delete(Node* node) {
        while (node != root_ && node->color == Color::Black) {
            if (node == node->parent->left) {
                Node* sibling = node->parent->right;
                if (sibling->color == Color::Red) {
                    sibling->color = Color::Black;
                    node->parent->color = Color::Red;
                    rotate_left(node->parent);
                    sibling = node->parent->right;
                }
                if (sibling->left->color == Color::Black && sibling->right->color == Color::Black) {
                    sibling->color = Color::Red;
                    node = node->parent;
                } else {
                    if (sibling->right->color == Color::Black) {
                        sibling->left->color = Color::Black;
                        sibling->color = Color::Red;
                        rotate_right(sibling);
                        sibling = node->parent->right;
                    }
                    sibling->color = node->parent->color;
                    node->parent->color = Color::Black;
                    sibling->right->color = Color::Black;
                    rotate_left(node->parent);
                    node = root_;
                }
            } else {
                Node* sibling = node->parent->left;
                if (sibling->color == Color::Red) {
                    sibling->color = Color::Black;
                    node->parent->color = Color::Red;
                    rotate_right(node->parent);
                    sibling = node->parent->left;
                }
                if (sibling->right->color == Color::Black && sibling->left->color == Color::Black) {
                    sibling->color = Color::Red;
                    node = node->parent;
                } else {
                    if (sibling->left->color == Color::Black) {
                        sibling->right->color = Color::Black;
                        sibling->color = Color::Red;
                        rotate_left(sibling);
                        sibling = node->parent->left;
                    }
                    sibling->color = node->parent->color;
                    node->parent->color = Color::Black;
                    sibling->left->color = Color::Black;
                    rotate_right(node->parent);
                    node = root_;
                }
            }
        }
        node->color = Color::Black;
    }

    void print_sorted(Node* node) const {
        if (node == nil_) {
            return;
        }
        print_sorted(node->left);
        std::cout << node->key << (node->color == Color::Red ? "R " : "B ");
        print_sorted(node->right);
    }
};

int main() {
    RedBlackTree tree;
    for (int key : std::vector<int>{41, 38, 31, 12, 19, 8, 55, 60, 3}) {
        tree.insert(key);
    }

    std::cout << "after inserts: ";
    tree.print_sorted();
    std::cout << "contains 19: " << (tree.contains(19) ? "yes" : "no") << '\n';

    tree.erase(38);
    tree.erase(12);
    std::cout << "after deletes: ";
    tree.print_sorted();
}
