// Lab 6 - B-Tree over integer keys
// Dhruv Davda (24BCS10203)
//
// A B-tree of minimum degree t. Every node (except the root) keeps between
// t-1 and 2t-1 keys, an internal node with k keys owns k+1 children, and all
// leaves sit at the same depth. The driver exposes insert, delete, search,
// a sorted (in-order) dump and a level-by-level dump.

#include <algorithm>
#include <iostream>
#include <queue>
#include <vector>

class BTree {
public:
    explicit BTree(int minDegree) : t(minDegree), root(nullptr) {}

    ~BTree() { release(root); }

    BTree(const BTree&) = delete;
    BTree& operator=(const BTree&) = delete;

    bool search(int key) const { return findNode(root, key) != nullptr; }

    void insert(int key) {
        if (search(key)) {                       // this B-tree stores distinct keys
            std::cout << "Key " << key << " already present; ignored.\n";
            return;
        }
        if (root == nullptr) {
            root = new Node(true);
            root->keys.push_back(key);
            return;
        }
        if (isFull(root)) {                      // a full root is the only way the tree grows taller
            Node* grown = new Node(false);
            grown->kids.push_back(root);
            splitChild(grown, 0);
            root = grown;
        }
        insertNonFull(root, key);
    }

    void erase(int key) {
        if (root == nullptr) {
            std::cout << "Tree is empty.\n";
            return;
        }
        if (!search(key)) {
            std::cout << "Key " << key << " not found.\n";
            return;
        }
        eraseFrom(root, key);
        if (root->keys.empty()) {                // the root emptied out after a merge
            Node* dead = root;
            root = root->isLeaf ? nullptr : root->kids.front();
            dead->kids.clear();
            delete dead;
        }
    }

    void printSorted() const {
        if (root == nullptr) {
            std::cout << "(empty)\n";
            return;
        }
        inorder(root);
        std::cout << '\n';
    }

    void printByLevel() const {
        if (root == nullptr) {
            std::cout << "(empty)\n";
            return;
        }
        std::queue<std::pair<const Node*, int>> q;
        q.push({root, 0});
        int shown = -1;
        while (!q.empty()) {
            auto [node, depth] = q.front();
            q.pop();
            if (depth != shown) {
                if (shown != -1) std::cout << '\n';
                std::cout << "Level " << depth << ":";
                shown = depth;
            }
            std::cout << " [";
            for (std::size_t i = 0; i < node->keys.size(); ++i) {
                if (i) std::cout << ' ';
                std::cout << node->keys[i];
            }
            std::cout << ']';
            for (const Node* c : node->kids) q.push({c, depth + 1});
        }
        std::cout << '\n';
    }

private:
    struct Node {
        bool isLeaf;
        std::vector<int> keys;
        std::vector<Node*> kids;
        explicit Node(bool leaf) : isLeaf(leaf) {}
    };

    int t;        // minimum degree (t >= 2)
    Node* root;

    bool isFull(const Node* n) const { return static_cast<int>(n->keys.size()) == 2 * t - 1; }

    static int slot(const Node* n, int key) {     // first index whose key is >= key
        return static_cast<int>(
            std::lower_bound(n->keys.begin(), n->keys.end(), key) - n->keys.begin());
    }

    static void release(Node* n) {
        if (n == nullptr) return;
        for (Node* c : n->kids) release(c);
        delete n;
    }

    const Node* findNode(const Node* n, int key) const {
        while (n != nullptr) {
            int i = slot(n, key);
            if (i < static_cast<int>(n->keys.size()) && n->keys[i] == key) return n;
            if (n->isLeaf) return nullptr;
            n = n->kids[i];
        }
        return nullptr;
    }

    // Split kids[i] of parent, which is assumed full. Its median rises into the
    // parent and the upper half becomes a brand new right sibling.
    void splitChild(Node* parent, int i) {
        Node* full = parent->kids[i];
        Node* sib = new Node(full->isLeaf);

        int median = full->keys[t - 1];
        sib->keys.assign(full->keys.begin() + t, full->keys.end());
        full->keys.resize(t - 1);

        if (!full->isLeaf) {
            sib->kids.assign(full->kids.begin() + t, full->kids.end());
            full->kids.resize(t);
        }

        parent->keys.insert(parent->keys.begin() + i, median);
        parent->kids.insert(parent->kids.begin() + i + 1, sib);
    }

    void insertNonFull(Node* n, int key) {
        if (n->isLeaf) {
            n->keys.insert(std::upper_bound(n->keys.begin(), n->keys.end(), key), key);
            return;
        }
        int i = slot(n, key);
        if (isFull(n->kids[i])) {
            splitChild(n, i);
            if (key > n->keys[i]) ++i;           // the median may now sit before our key
        }
        insertNonFull(n->kids[i], key);
    }

    void inorder(const Node* n) const {
        for (std::size_t i = 0; i < n->keys.size(); ++i) {
            if (!n->isLeaf) inorder(n->kids[i]);
            std::cout << n->keys[i] << ' ';
        }
        if (!n->isLeaf) inorder(n->kids.back());
    }

    int rightmostKey(const Node* n) const {        // in-order predecessor source
        while (!n->isLeaf) n = n->kids.back();
        return n->keys.back();
    }

    int leftmostKey(const Node* n) const {          // in-order successor source
        while (!n->isLeaf) n = n->kids.front();
        return n->keys.front();
    }

    // Fold kids[i], the separator keys[i] and kids[i+1] into a single child.
    void merge(Node* parent, int i) {
        Node* left = parent->kids[i];
        Node* right = parent->kids[i + 1];

        left->keys.push_back(parent->keys[i]);
        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
        if (!left->isLeaf)
            left->kids.insert(left->kids.end(), right->kids.begin(), right->kids.end());

        parent->keys.erase(parent->keys.begin() + i);
        parent->kids.erase(parent->kids.begin() + i + 1);

        right->kids.clear();
        delete right;
    }

    void borrowFromLeft(Node* parent, int i) {       // kids[i] pulls a key from kids[i-1]
        Node* child = parent->kids[i];
        Node* left = parent->kids[i - 1];

        child->keys.insert(child->keys.begin(), parent->keys[i - 1]);
        parent->keys[i - 1] = left->keys.back();
        left->keys.pop_back();

        if (!child->isLeaf) {
            child->kids.insert(child->kids.begin(), left->kids.back());
            left->kids.pop_back();
        }
    }

    void borrowFromRight(Node* parent, int i) {      // kids[i] pulls a key from kids[i+1]
        Node* child = parent->kids[i];
        Node* right = parent->kids[i + 1];

        child->keys.push_back(parent->keys[i]);
        parent->keys[i] = right->keys.front();
        right->keys.erase(right->keys.begin());

        if (!child->isLeaf) {
            child->kids.push_back(right->kids.front());
            right->kids.erase(right->kids.begin());
        }
    }

    // Guarantee kids[i] has at least t keys before we step into it.
    void refill(Node* parent, int i) {
        if (i > 0 && static_cast<int>(parent->kids[i - 1]->keys.size()) >= t) {
            borrowFromLeft(parent, i);
        } else if (i < static_cast<int>(parent->keys.size()) &&
                   static_cast<int>(parent->kids[i + 1]->keys.size()) >= t) {
            borrowFromRight(parent, i);
        } else if (i < static_cast<int>(parent->keys.size())) {
            merge(parent, i);
        } else {
            merge(parent, i - 1);
        }
    }

    void eraseFrom(Node* n, int key) {
        int i = slot(n, key);

        if (i < static_cast<int>(n->keys.size()) && n->keys[i] == key) {
            if (n->isLeaf) {                         // case 1: sitting in a leaf
                n->keys.erase(n->keys.begin() + i);
                return;
            }
            // case 2: sitting in an internal node
            if (static_cast<int>(n->kids[i]->keys.size()) >= t) {
                int pred = rightmostKey(n->kids[i]);
                n->keys[i] = pred;
                eraseFrom(n->kids[i], pred);
            } else if (static_cast<int>(n->kids[i + 1]->keys.size()) >= t) {
                int succ = leftmostKey(n->kids[i + 1]);
                n->keys[i] = succ;
                eraseFrom(n->kids[i + 1], succ);
            } else {
                merge(n, i);                         // both neighbours minimal -> merge and recurse
                eraseFrom(n->kids[i], key);
            }
            return;
        }

        if (n->isLeaf) return;                        // not in the tree (guarded by search())

        bool wentRightmost = (i == static_cast<int>(n->keys.size()));
        if (static_cast<int>(n->kids[i]->keys.size()) < t) refill(n, i);

        // A merge at the far right shifts the child we wanted one slot left.
        if (wentRightmost && i > static_cast<int>(n->keys.size()))
            eraseFrom(n->kids[i - 1], key);
        else
            eraseFrom(n->kids[i], key);
    }
};

int main() {
    int t = 0;
    std::cout << "Minimum degree t (t >= 2): ";
    if (!(std::cin >> t) || t < 2) {
        std::cout << "Minimum degree must be an integer >= 2.\n";
        return 1;
    }

    BTree tree(t);

    while (true) {
        std::cout << "\n--- B-Tree menu ---\n"
                  << "1) Insert\n"
                  << "2) Delete\n"
                  << "3) Search\n"
                  << "4) Print sorted (in-order)\n"
                  << "5) Print by level\n"
                  << "6) Quit\n"
                  << "Choice: ";

        int choice = 0;
        if (!(std::cin >> choice)) break;            // EOF / bad input ends the session
        if (choice == 6) break;

        int key = 0;
        switch (choice) {
            case 1:
                std::cout << "Key to insert: ";
                if (std::cin >> key) tree.insert(key);
                break;
            case 2:
                std::cout << "Key to delete: ";
                if (std::cin >> key) tree.erase(key);
                break;
            case 3:
                std::cout << "Key to search: ";
                if (std::cin >> key)
                    std::cout << (tree.search(key) ? "Present\n" : "Absent\n");
                break;
            case 4:
                tree.printSorted();
                break;
            case 5:
                tree.printByLevel();
                break;
            default:
                std::cout << "Pick a number from 1 to 6.\n";
                break;
        }
    }

    return 0;
}
