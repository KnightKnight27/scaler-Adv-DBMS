#include <climits>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

class RedBlackTree {
private:
    enum class Color { Red, Black };

    struct Node {
        int key;
        Color color;
        Node* left;
        Node* right;
        Node* parent;

        Node(int value, Color shade, Node* nil)
            : key(value), color(shade), left(nil), right(nil), parent(nil) {}
    };

    Node* root_;
    Node* nil_;

    static const char* colorLabel(Color color) {
        return color == Color::Red ? "R" : "B";
    }

    void rotateLeft(Node* pivot) {
        Node* lifted = pivot->right;
        pivot->right = lifted->left;

        if (lifted->left != nil_) {
            lifted->left->parent = pivot;
        }

        lifted->parent = pivot->parent;

        if (pivot->parent == nil_) {
            root_ = lifted;
        } else if (pivot == pivot->parent->left) {
            pivot->parent->left = lifted;
        } else {
            pivot->parent->right = lifted;
        }

        lifted->left = pivot;
        pivot->parent = lifted;
    }

    void rotateRight(Node* pivot) {
        Node* lifted = pivot->left;
        pivot->left = lifted->right;

        if (lifted->right != nil_) {
            lifted->right->parent = pivot;
        }

        lifted->parent = pivot->parent;

        if (pivot->parent == nil_) {
            root_ = lifted;
        } else if (pivot == pivot->parent->right) {
            pivot->parent->right = lifted;
        } else {
            pivot->parent->left = lifted;
        }

        lifted->right = pivot;
        pivot->parent = lifted;
    }

    void repairInsertion(Node* node) {
        while (node->parent->color == Color::Red) {
            if (node->parent == node->parent->parent->left) {
                Node* uncle = node->parent->parent->right;

                // Case 1: parent and uncle are red, so recolor and move upward.
                if (uncle->color == Color::Red) {
                    node->parent->color = Color::Black;
                    uncle->color = Color::Black;
                    node->parent->parent->color = Color::Red;
                    node = node->parent->parent;
                } else {
                    // Case 2: triangle shape; rotate once to convert it to a line.
                    if (node == node->parent->right) {
                        node = node->parent;
                        rotateLeft(node);
                    }

                    // Case 3: line shape; recolor and rotate around grandparent.
                    node->parent->color = Color::Black;
                    node->parent->parent->color = Color::Red;
                    rotateRight(node->parent->parent);
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
                        rotateRight(node);
                    }

                    node->parent->color = Color::Black;
                    node->parent->parent->color = Color::Red;
                    rotateLeft(node->parent->parent);
                }
            }
        }

        root_->color = Color::Black;
    }

    Node* findNode(int key) const {
        Node* cursor = root_;

        while (cursor != nil_) {
            if (key < cursor->key) {
                cursor = cursor->left;
            } else if (key > cursor->key) {
                cursor = cursor->right;
            } else {
                return cursor;
            }
        }

        return nil_;
    }

    void inorderWalk(Node* node) const {
        if (node == nil_) {
            return;
        }

        inorderWalk(node->left);
        std::cout << node->key << "(" << colorLabel(node->color) << ") ";
        inorderWalk(node->right);
    }

    void clearSubtree(Node* node) {
        if (node == nil_) {
            return;
        }

        clearSubtree(node->left);
        clearSubtree(node->right);
        delete node;
    }

    int validateSubtree(Node* node, long long lower, long long upper, bool& ok) const {
        if (node == nil_) {
            return 1;
        }

        if (node->key <= lower || node->key >= upper) {
            ok = false;
        }

        if (node->color == Color::Red) {
            if (node->left->color == Color::Red || node->right->color == Color::Red) {
                ok = false;
            }
        }

        int leftBlackHeight = validateSubtree(node->left, lower, node->key, ok);
        int rightBlackHeight = validateSubtree(node->right, node->key, upper, ok);

        if (leftBlackHeight != rightBlackHeight) {
            ok = false;
        }

        return leftBlackHeight + (node->color == Color::Black ? 1 : 0);
    }

public:
    RedBlackTree() {
        nil_ = new Node(0, Color::Black, nullptr);
        nil_->left = nil_;
        nil_->right = nil_;
        nil_->parent = nil_;
        root_ = nil_;
    }

    ~RedBlackTree() {
        clearSubtree(root_);
        delete nil_;
    }

    bool insert(int key) {
        Node* parent = nil_;
        Node* cursor = root_;

        while (cursor != nil_) {
            parent = cursor;

            if (key < cursor->key) {
                cursor = cursor->left;
            } else if (key > cursor->key) {
                cursor = cursor->right;
            } else {
                return false;
            }
        }

        Node* fresh = new Node(key, Color::Red, nil_);
        fresh->parent = parent;

        if (parent == nil_) {
            root_ = fresh;
        } else if (key < parent->key) {
            parent->left = fresh;
        } else {
            parent->right = fresh;
        }

        repairInsertion(fresh);
        return true;
    }

    bool contains(int key) const {
        return findNode(key) != nil_;
    }

    void printInorder() const {
        inorderWalk(root_);
        std::cout << '\n';
    }

    void printLevelOrder() const {
        if (root_ == nil_) {
            std::cout << "(empty)\n";
            return;
        }

        std::queue<Node*> pending;
        pending.push(root_);
        int level = 0;

        while (!pending.empty()) {
            std::size_t nodesThisLevel = pending.size();
            std::cout << "Level " << level << ": ";

            for (std::size_t i = 0; i < nodesThisLevel; ++i) {
                Node* current = pending.front();
                pending.pop();

                std::cout << current->key << "(" << colorLabel(current->color) << ") ";

                if (current->left != nil_) {
                    pending.push(current->left);
                }
                if (current->right != nil_) {
                    pending.push(current->right);
                }
            }

            std::cout << '\n';
            ++level;
        }
    }

    bool validate() const {
        if (root_ == nil_) {
            return true;
        }

        if (root_->color != Color::Black) {
            return false;
        }

        bool ok = true;
        validateSubtree(root_, LLONG_MIN, LLONG_MAX, ok);
        return ok;
    }
};

int main() {
    RedBlackTree tree;
    std::vector<int> insertionOrder = {42, 17, 68, 9, 23, 55, 79, 3, 12, 19, 27, 50, 60, 72, 88, 30, 26};

    std::cout << "Insertion sequence: ";
    for (int value : insertionOrder) {
        std::cout << value << ' ';
        tree.insert(value);
    }
    std::cout << "\n\n";

    std::cout << "Inorder traversal:\n";
    tree.printInorder();
    std::cout << '\n';

    std::cout << "Level order traversal with colors:\n";
    tree.printLevelOrder();
    std::cout << '\n';

    int presentKey = 60;
    int missingKey = 99;

    std::cout << "Search " << presentKey << ": "
              << (tree.contains(presentKey) ? "found" : "not found") << '\n';
    std::cout << "Search " << missingKey << ": "
              << (tree.contains(missingKey) ? "found" : "not found") << '\n';

    int duplicateKey = 23;
    bool inserted = tree.insert(duplicateKey);
    std::cout << "Duplicate insert " << duplicateKey << ": "
              << (inserted ? "inserted" : "ignored") << '\n';

    std::cout << "Validation result: "
              << (tree.validate() ? "valid Red-Black Tree" : "invalid Red-Black Tree")
              << '\n';

    return 0;
}
