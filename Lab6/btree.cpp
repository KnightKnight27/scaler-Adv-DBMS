// Lab 6 - B-Tree
// Author: 24BCS10345 Ansh Mahajan
//
// A balanced multi-way search tree. For a chosen minimum degree t (t >= 2):
//   - every node holds between t-1 and 2t-1 keys (the root may hold fewer),
//   - an internal node with k keys has exactly k+1 children,
//   - keys inside a node are sorted, and all leaves sit at the same depth.
//
// Insertion uses the proactive (top-down) scheme: while descending we split
// any full child (2t-1 keys) *before* stepping into it, so the recursion never
// has to back up to fix an overflow. Splitting the root is the only way the
// tree grows in height, which keeps every leaf on the same level.
//
// API:
//   BTree<T>(minDegree)
//     void insert(const T& key)        - insert a key (duplicates ignored)
//     bool search(const T& key)        - true if the key is present
//     void inorder()                   - keys in sorted order
//     void printByLevel()              - node contents, one tree level per line
//
// Build: g++ -std=c++17 btree.cpp -o btree
// Run:   ./btree   (menu-driven)

#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <vector>

template <typename T>
class BTree {
public:
    explicit BTree(int minDegree) : t_(minDegree), root_(nullptr) {
        if (minDegree < 2) {
            throw std::invalid_argument("B-Tree minimum degree must be >= 2");
        }
    }

    ~BTree() { destroy(root_); }

    BTree(const BTree&) = delete;
    BTree& operator=(const BTree&) = delete;

    void insert(const T& key) {
        if (root_ == nullptr) {
            root_ = new Node(true);
            root_->keys.push_back(key);
            return;
        }
        if (search(key)) {
            return;                       // keep keys unique for a clean demo
        }
        // If the root is full, grow upward first: make a fresh empty root and
        // split the old one beneath it. This is the only height increase.
        if (static_cast<int>(root_->keys.size()) == maxKeys()) {
            Node* newRoot = new Node(false);
            newRoot->children.push_back(root_);
            splitChild(newRoot, 0);
            root_ = newRoot;
        }
        insertNonFull(root_, key);
    }

    bool search(const T& key) const { return search(root_, key); }

    void inorder() const {
        if (root_ == nullptr) {
            std::cout << "(empty)";
        } else {
            bool first = true;
            inorder(root_, first);
        }
        std::cout << '\n';
    }

    // Breadth-first dump: each line is one level, each [..] is one node.
    void printByLevel() const {
        if (root_ == nullptr) {
            std::cout << "(empty tree)\n";
            return;
        }
        std::queue<const Node*> current;
        current.push(root_);
        int level = 0;
        while (!current.empty()) {
            std::queue<const Node*> next;
            std::cout << "  L" << level << ": ";
            while (!current.empty()) {
                const Node* node = current.front();
                current.pop();
                std::cout << '[';
                for (std::size_t i = 0; i < node->keys.size(); ++i) {
                    std::cout << node->keys[i]
                              << (i + 1 < node->keys.size() ? " " : "");
                }
                std::cout << "] ";
                for (Node* child : node->children) {
                    next.push(child);
                }
            }
            std::cout << '\n';
            current = std::move(next);
            ++level;
        }
    }

private:
    struct Node {
        bool leaf;
        std::vector<T> keys;
        std::vector<Node*> children;
        explicit Node(bool isLeaf) : leaf(isLeaf) {}
    };

    int t_;          // minimum degree
    Node* root_;

    int maxKeys() const { return 2 * t_ - 1; }

    // Split the full child at children[index] of `parent`. The median key is
    // promoted into `parent`; the child's right half becomes a new sibling.
    void splitChild(Node* parent, int index) {
        Node* full = parent->children[index];
        Node* sibling = new Node(full->leaf);

        // Right t-1 keys move to the new sibling.
        for (int j = 0; j < t_ - 1; ++j) {
            sibling->keys.push_back(full->keys[t_ + j]);
        }
        // Right t children move too, when `full` is internal.
        if (!full->leaf) {
            for (int j = 0; j < t_; ++j) {
                sibling->children.push_back(full->children[t_ + j]);
            }
            full->children.resize(t_);
        }

        T median = full->keys[t_ - 1];
        full->keys.resize(t_ - 1);        // left t-1 keys stay in `full`

        parent->children.insert(parent->children.begin() + index + 1, sibling);
        parent->keys.insert(parent->keys.begin() + index, median);
    }

    // Insert into a node guaranteed not to be full.
    void insertNonFull(Node* node, const T& key) {
        int i = static_cast<int>(node->keys.size()) - 1;
        if (node->leaf) {
            node->keys.push_back(key);            // grow, then shift into place
            while (i >= 0 && key < node->keys[i]) {
                node->keys[i + 1] = node->keys[i];
                --i;
            }
            node->keys[i + 1] = key;
            return;
        }
        // Find the child that should receive the key.
        while (i >= 0 && key < node->keys[i]) {
            --i;
        }
        ++i;
        if (static_cast<int>(node->children[i]->keys.size()) == maxKeys()) {
            splitChild(node, i);
            if (node->keys[i] < key) {            // median may shift the target
                ++i;
            }
        }
        insertNonFull(node->children[i], key);
    }

    bool search(const Node* node, const T& key) const {
        if (node == nullptr) {
            return false;
        }
        std::size_t i = 0;
        while (i < node->keys.size() && node->keys[i] < key) {
            ++i;
        }
        if (i < node->keys.size() && !(key < node->keys[i])) {
            return true;                          // exact match
        }
        if (node->leaf) {
            return false;
        }
        return search(node->children[i], key);
    }

    void inorder(const Node* node, bool& first) const {
        std::size_t i = 0;
        for (; i < node->keys.size(); ++i) {
            if (!node->leaf) {
                inorder(node->children[i], first);
            }
            if (!first) {
                std::cout << ", ";
            }
            first = false;
            std::cout << node->keys[i];
        }
        if (!node->leaf) {
            inorder(node->children[i], first);    // right-most child
        }
    }

    void destroy(Node* node) {
        if (node == nullptr) {
            return;
        }
        for (Node* child : node->children) {
            destroy(child);
        }
        delete node;
    }
};

namespace {

void printMenu(int t) {
    std::cout << "\nB-Tree menu (minimum degree t=" << t << ", max "
              << (2 * t - 1) << " keys/node)\n"
              << "  1) Insert key\n"
              << "  2) Search key\n"
              << "  3) Show inorder traversal\n"
              << "  4) Show tree structure (by level)\n"
              << "  5) Exit\n"
              << "Choice: ";
}

// Discard the rest of a bad input line so the menu loop can recover.
void clearLine() {
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

}  // namespace

int main() {
    const int t = 3;                 // max 5 keys per node -> visible splits
    BTree<int> tree(t);

    int choice = 0;
    while (true) {
        printMenu(t);
        if (!(std::cin >> choice)) {
            if (std::cin.eof()) {
                break;               // allow piped scripts to end cleanly
            }
            std::cout << "Please enter a number.\n";
            clearLine();
            continue;
        }

        if (choice == 5) {
            std::cout << "Bye.\n";
            break;
        }

        int key = 0;
        switch (choice) {
            case 1:
                std::cout << "Key to insert: ";
                if (std::cin >> key) {
                    tree.insert(key);
                    std::cout << "Inserted " << key << ".\n";
                }
                break;
            case 2:
                std::cout << "Key to search: ";
                if (std::cin >> key) {
                    std::cout << (tree.search(key) ? "Found" : "Not found")
                              << ' ' << key << ".\n";
                }
                break;
            case 3:
                std::cout << "Inorder: ";
                tree.inorder();
                break;
            case 4:
                std::cout << "Structure:\n";
                tree.printByLevel();
                break;
            default:
                std::cout << "Unknown choice.\n";
                break;
        }
    }
    return 0;
}
