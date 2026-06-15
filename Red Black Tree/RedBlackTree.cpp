#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <vector>

enum class NodeColor {
    RED,
    BLACK
};

struct RBNode {
    int value;
    NodeColor node_color;
    RBNode* p;
    RBNode* left_child;
    RBNode* right_child;

    explicit RBNode(int val)
        : value(val), node_color(NodeColor::RED), p(nullptr), left_child(nullptr), right_child(nullptr)
    {
    }
};

class RBTree {
public:
    RBTree() = default;

    ~RBTree() {
        clearTree(root_node);
    }

    void insertValue(int val) {
        RBNode* parent_ptr = nullptr;
        RBNode* iter = root_node;

        while (iter != nullptr) {
            parent_ptr = iter;

            if (val == iter->value) {
                return;
            }

            iter = (val < iter->value) ? iter->left_child : iter->right_child;
        }

        RBNode* new_node = new RBNode(val);
        new_node->p = parent_ptr;

        if (parent_ptr == nullptr) {
            root_node = new_node;
        } else if (val < parent_ptr->value) {
            parent_ptr->left_child = new_node;
        } else {
            parent_ptr->right_child = new_node;
        }

        balanceAfterInsert(new_node);
    }

    bool search(int val) const {
        RBNode* iter = root_node;

        while (iter != nullptr) {
            if (val == iter->value) {
                return true;
            }

            iter = (val < iter->value) ? iter->left_child : iter->right_child;
        }

        return false;
    }

    void displayInOrder() const {
        std::cout << "Tree In-Order: ";
        displayInOrder(root_node);
        std::cout << '\n';
    }

    void displayLevelOrder() const {
        if (root_node == nullptr) {
            std::cout << "Tree is currently empty.\n";
            return;
        }

        std::queue<RBNode*> node_q;
        node_q.push(root_node);

        std::cout << "Tree Level-Order: ";

        while (!node_q.empty()) {
            RBNode* front_node = node_q.front();
            node_q.pop();

            std::cout << front_node->value << getColorStr(front_node) << " ";

            if (front_node->left_child != nullptr) {
                node_q.push(front_node->left_child);
            }

            if (front_node->right_child != nullptr) {
                node_q.push(front_node->right_child);
            }
        }

        std::cout << '\n';
    }

    bool checkValidity() const {
        if (root_node == nullptr) return true;

        if (root_node->node_color != NodeColor::BLACK) return false;

        int expected_bh = -1;
        return checkTreeRules(
            root_node,
            std::numeric_limits<int>::min(),
            std::numeric_limits<int>::max(),
            0,
            expected_bh
        );
    }

private:
    RBNode* root_node{nullptr};

    static NodeColor fetchColor(RBNode* n) {
        return n == nullptr ? NodeColor::BLACK : n->node_color;
    }

    static std::string getColorStr(RBNode* n) {
        return fetchColor(n) == NodeColor::RED ? "[R]" : "[B]";
    }

    void leftRotate(RBNode* x) {
        RBNode* y = x->right_child;
        x->right_child = y->left_child;

        if (y->left_child != nullptr) {
            y->left_child->p = x;
        }

        y->p = x->p;

        if (x->p == nullptr) {
            root_node = y;
        } else if (x == x->p->left_child) {
            x->p->left_child = y;
        } else {
            x->p->right_child = y;
        }

        y->left_child = x;
        x->p = y;
    }

    void rightRotate(RBNode* x) {
        RBNode* y = x->left_child;
        x->left_child = y->right_child;

        if (y->right_child != nullptr) {
            y->right_child->p = x;
        }

        y->p = x->p;

        if (x->p == nullptr) {
            root_node = y;
        } else if (x == x->p->right_child) {
            x->p->right_child = y;
        } else {
            x->p->left_child = y;
        }

        y->right_child = x;
        x->p = y;
    }

    void balanceAfterInsert(RBNode* z) {
        while (z != root_node && fetchColor(z->p) == NodeColor::RED) {
            RBNode* parent_node = z->p;
            RBNode* grand_parent = parent_node->p;

            if (parent_node == grand_parent->left_child) {
                RBNode* uncle = grand_parent->right_child;

                if (fetchColor(uncle) == NodeColor::RED) {
                    parent_node->node_color = NodeColor::BLACK;
                    uncle->node_color = NodeColor::BLACK;
                    grand_parent->node_color = NodeColor::RED;
                    z = grand_parent;
                } else {
                    if (z == parent_node->right_child) {
                        z = parent_node;
                        leftRotate(z);
                        parent_node = z->p;
                        grand_parent = parent_node->p;
                    }

                    parent_node->node_color = NodeColor::BLACK;
                    grand_parent->node_color = NodeColor::RED;
                    rightRotate(grand_parent);
                }
            } else {
                RBNode* uncle = grand_parent->left_child;

                if (fetchColor(uncle) == NodeColor::RED) {
                    parent_node->node_color = NodeColor::BLACK;
                    uncle->node_color = NodeColor::BLACK;
                    grand_parent->node_color = NodeColor::RED;
                    z = grand_parent;
                } else {
                    if (z == parent_node->left_child) {
                        z = parent_node;
                        rightRotate(z);
                        parent_node = z->p;
                        grand_parent = parent_node->p;
                    }

                    parent_node->node_color = NodeColor::BLACK;
                    grand_parent->node_color = NodeColor::RED;
                    leftRotate(grand_parent);
                }
            }
        }

        root_node->node_color = NodeColor::BLACK;
    }

    void displayInOrder(RBNode* n) const {
        if (n == nullptr) return;

        displayInOrder(n->left_child);
        std::cout << n->value << getColorStr(n) << " ";
        displayInOrder(n->right_child);
    }

    bool checkTreeRules(RBNode* n, int min_val, int max_val, int black_count, int& bh_target) const {
        if (n == nullptr) {
            if (bh_target == -1) {
                bh_target = black_count;
                return true;
            }
            return black_count == bh_target;
        }

        if (n->value <= min_val || n->value >= max_val) {
            return false;
        }

        if (n->node_color == NodeColor::BLACK) {
            ++black_count;
        }

        if (n->node_color == NodeColor::RED) {
            if (fetchColor(n->left_child) == NodeColor::RED || fetchColor(n->right_child) == NodeColor::RED) {
                return false;
            }
        }

        return checkTreeRules(n->left_child, min_val, n->value, black_count, bh_target)
            && checkTreeRules(n->right_child, n->value, max_val, black_count, bh_target);
    }

    void clearTree(RBNode* n) {
        if (n == nullptr) return;
        clearTree(n->left_child);
        clearTree(n->right_child);
        delete n;
    }
};

int main() {
    RBTree tree_inst;
    std::vector<int> test_data = { 41, 38, 31, 12, 19, 8, 25, 50, 60, 55, 5, 1, 70 };
    int target_present = 25;
    int target_absent = 99;

    for (int v : test_data) {
        tree_inst.insertValue(v);
        std::cout << "Added " << v << " | Valid: " << (tree_inst.checkValidity() ? "YES" : "NO") << '\n';
    }

    std::cout << '\n';
    tree_inst.displayInOrder();
    tree_inst.displayLevelOrder();

    auto lookup_print = [&tree_inst](int k) {
        std::cout << "Looking up " << k << " -> " << (tree_inst.search(k) ? "Found" : "Missing") << '\n';
    };

    lookup_print(target_present);
    lookup_print(target_absent);
    std::cout << "Tree Integrity Check: " << (tree_inst.checkValidity() ? "PASSED" : "FAILED") << '\n';

    return 0;
}