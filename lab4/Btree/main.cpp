/*
 * ==========================================================
 * Lab 6 - B-Tree Index Implementation
 *
 * Name  : Jatin Chulet
 * Roll  : 24BCS10213
 *
 * A B-Tree is a self-balancing multi-way search tree used
 * extensively in databases and file systems. Each node can
 * store multiple keys, reducing tree height and improving
 * search performance.
 *
 * Operations:
 * - Insert
 * - Search
 * - Delete
 * - Level Order Traversal
 * - Inorder Traversal
 * ==========================================================
 */

#include <iostream>
#include <vector>

class BTree {
public:
    explicit BTree(int t) : t_(t < 2 ? 2 : t), root_(nullptr) {}
    ~BTree() { destroy(root_); }

    void insert(int key) {

        // Create root if tree is empty
        if (root_ == nullptr) {
            root_ = new Node(true);
            root_->keys.push_back(key);
            return;
        }

        // Root split is the only operation that increases tree height
        if (full(root_)) {
            Node* fresh = new Node(false);
            fresh->kids.push_back(root_);
            split_child(fresh, 0);
            root_ = fresh;
        }

        insert_nonfull(root_, key);
    }

    bool search(int key) const {
        return find(root_, key);
    }

    void remove(int key) {

        if (root_ == nullptr)
            return;

        erase(root_, key);

        if (root_->keys.empty() && !root_->leaf) {
            Node* old = root_;
            root_ = root_->kids[0];
            old->kids.clear();
            delete old;
        }
        else if (root_->keys.empty() && root_->leaf) {
            delete root_;
            root_ = nullptr;
        }
    }

    void print_levels() const {

        if (!root_) {
            std::cout << "(empty)\n";
            return;
        }

        std::vector<Node*> level = {root_};
        int depth = 0;

        while (!level.empty()) {

            std::cout << "L" << depth << ": ";

            std::vector<Node*> next;

            for (Node* n : level) {

                std::cout << "[";

                for (size_t i = 0; i < n->keys.size(); ++i)
                    std::cout << n->keys[i]
                              << (i + 1 < n->keys.size() ? " " : "");

                std::cout << "] ";

                for (Node* c : n->kids)
                    next.push_back(c);
            }

            std::cout << "\n";
            level = next;
            ++depth;
        }
    }

    void print_sorted() const {
        inorder(root_);
        std::cout << "\n";
    }

private:

    // Structure representing a B-Tree node
    struct Node {
        std::vector<int> keys;
        std::vector<Node*> kids;
        bool leaf;

        explicit Node(bool is_leaf)
            : leaf(is_leaf) {}
    };

    int t_;
    Node* root_;

    bool full(Node* n) const {
        return static_cast<int>(n->keys.size()) == 2 * t_ - 1;
    }

    void destroy(Node* n) {

        if (!n)
            return;

        for (Node* c : n->kids)
            destroy(c);

        delete n;
    }

    bool find(Node* n, int key) const {

        if (!n)
            return false;

        size_t i = 0;

        while (i < n->keys.size() && key > n->keys[i])
            ++i;

        if (i < n->keys.size() && key == n->keys[i])
            return true;

        if (n->leaf)
            return false;

        return find(n->kids[i], key);
    }

    // Split a full child node and promote the middle key
    void split_child(Node* parent, int i) {

        Node* y = parent->kids[i];
        Node* z = new Node(y->leaf);

        int mid = y->keys[t_ - 1];

        z->keys.assign(y->keys.begin() + t_, y->keys.end());

        y->keys.resize(t_ - 1);

        if (!y->leaf) {
            z->kids.assign(y->kids.begin() + t_, y->kids.end());
            y->kids.resize(t_);
        }

        parent->kids.insert(parent->kids.begin() + i + 1, z);
        parent->keys.insert(parent->keys.begin() + i, mid);
    }

    // Insert into a node that is guaranteed not to be full
    void insert_nonfull(Node* n, int key) {

        int i = static_cast<int>(n->keys.size()) - 1;

        if (n->leaf) {

            n->keys.push_back(0);

            while (i >= 0 && key < n->keys[i]) {
                n->keys[i + 1] = n->keys[i];
                --i;
            }

            n->keys[i + 1] = key;
            return;
        }

        while (i >= 0 && key < n->keys[i])
            --i;

        ++i;

        if (full(n->kids[i])) {

            split_child(n, i);

            if (key > n->keys[i])
                ++i;
        }

        insert_nonfull(n->kids[i], key);
    }

    int max_key(Node* n) const {
        while (!n->leaf)
            n = n->kids.back();

        return n->keys.back();
    }

    int min_key(Node* n) const {
        while (!n->leaf)
            n = n->kids.front();

        return n->keys.front();
    }

    // Merge two sibling nodes during deletion
    void merge(Node* n, int i) {

        Node* left = n->kids[i];
        Node* right = n->kids[i + 1];

        left->keys.push_back(n->keys[i]);

        left->keys.insert(
            left->keys.end(),
            right->keys.begin(),
            right->keys.end()
        );

        if (!left->leaf) {
            left->kids.insert(
                left->kids.end(),
                right->kids.begin(),
                right->kids.end()
            );
        }

        n->keys.erase(n->keys.begin() + i);
        n->kids.erase(n->kids.begin() + i + 1);

        right->kids.clear();
        delete right;
    }

    // Ensure child has minimum required keys before descending
    void ensure_min(Node* n, int i) {

        if (static_cast<int>(n->kids[i]->keys.size()) >= t_)
            return;

        if (i > 0 &&
            static_cast<int>(n->kids[i - 1]->keys.size()) >= t_) {

            Node* child = n->kids[i];
            Node* sibling = n->kids[i - 1];

            child->keys.insert(
                child->keys.begin(),
                n->keys[i - 1]
            );

            n->keys[i - 1] = sibling->keys.back();
            sibling->keys.pop_back();

            if (!child->leaf) {
                child->kids.insert(
                    child->kids.begin(),
                    sibling->kids.back()
                );
                sibling->kids.pop_back();
            }
        }
        else if (
            i < static_cast<int>(n->keys.size()) &&
            static_cast<int>(n->kids[i + 1]->keys.size()) >= t_) {

            Node* child = n->kids[i];
            Node* sibling = n->kids[i + 1];

            child->keys.push_back(n->keys[i]);

            n->keys[i] = sibling->keys.front();

            sibling->keys.erase(sibling->keys.begin());

            if (!child->leaf) {
                child->kids.push_back(sibling->kids.front());
                sibling->kids.erase(sibling->kids.begin());
            }
        }
        else {

            if (i < static_cast<int>(n->keys.size()))
                merge(n, i);
            else
                merge(n, i - 1);
        }
    }

    // Recursive deletion procedure
    void erase(Node* n, int key) {

        int i = 0;

        while (i < static_cast<int>(n->keys.size()) &&
               key > n->keys[i])
            ++i;

        if (i < static_cast<int>(n->keys.size()) &&
            n->keys[i] == key) {

            if (n->leaf) {
                n->keys.erase(n->keys.begin() + i);
            }
            else if (
                static_cast<int>(n->kids[i]->keys.size()) >= t_) {

                int pred = max_key(n->kids[i]);
                n->keys[i] = pred;
                erase(n->kids[i], pred);
            }
            else if (
                static_cast<int>(n->kids[i + 1]->keys.size()) >= t_) {

                int succ = min_key(n->kids[i + 1]);
                n->keys[i] = succ;
                erase(n->kids[i + 1], succ);
            }
            else {

                merge(n, i);
                erase(n->kids[i], key);
            }

            return;
        }

        if (n->leaf)
            return;

        bool last =
            (i == static_cast<int>(n->keys.size()));

        ensure_min(n, i);

        if (last &&
            i > static_cast<int>(n->keys.size()))
            erase(n->kids[i - 1], key);
        else
            erase(n->kids[i], key);
    }

    // Inorder traversal prints keys in sorted order
    void inorder(Node* n) const {

        if (!n)
            return;

        for (size_t i = 0; i < n->keys.size(); ++i) {

            if (!n->leaf)
                inorder(n->kids[i]);

            std::cout << n->keys[i] << " ";
        }

        if (!n->leaf)
            inorder(n->kids.back());
    }
};

int main() {

    std::cout << "B-Tree Index Implementation\n";
    std::cout << "Jatin Chulet | 24BCS10213\n\n";

    BTree tree(2);

    const std::vector<int> keys = {
        15, 25, 5, 35, 45,
        10, 20, 30, 40, 50,
        60, 12, 18, 28
    };

    std::cout << "Inserted Keys: ";

    for (int key : keys) {
        std::cout << key << " ";
        tree.insert(key);
    }

    std::cout << "\n\n";

    std::cout << "Tree Structure By Levels:\n";
    tree.print_levels();

    std::cout << "\nSorted Keys: ";
    tree.print_sorted();

    std::cout << "\nSearch 28 = "
              << (tree.search(28) ? "Found" : "Not Found");

    std::cout << "\nSearch 99 = "
              << (tree.search(99) ? "Found" : "Not Found");

    std::cout << "\n\nRemoving 10 and 35...\n";

    tree.remove(10);
    tree.remove(35);

    tree.print_levels();

    std::cout << "Sorted Keys After Deletion: ";
    tree.print_sorted();

    return 0;
}