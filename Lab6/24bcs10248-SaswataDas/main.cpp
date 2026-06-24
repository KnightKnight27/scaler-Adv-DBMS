#include <algorithm>
#include <iostream>
#include <queue>
#include <vector>

class BTree {
private:
    struct Node {
        bool leaf;
        std::vector<int> keys;
        std::vector<Node*> child;

        explicit Node(bool isLeaf) : leaf(isLeaf) {}
    };

    Node* root;
    int degree;

    static void destroy(Node* node) {
        if (node == nullptr) {
            return;
        }

        for (Node* next : node->child) {
            destroy(next);
        }

        delete node;
    }

    static int lowerBoundIndex(const Node* node, int key) {
        return static_cast<int>(
            std::lower_bound(node->keys.begin(), node->keys.end(), key) - node->keys.begin()
        );
    }

    bool contains(const Node* node, int key) const {
        const int pos = lowerBoundIndex(node, key);

        if (pos < static_cast<int>(node->keys.size()) && node->keys[pos] == key) {
            return true;
        }

        if (node->leaf) {
            return false;
        }

        return contains(node->child[pos], key);
    }

    void splitChild(Node* parent, int childIndex) {
        Node* full = parent->child[childIndex];
        Node* right = new Node(full->leaf);

        const int medianIndex = degree - 1;
        const int median = full->keys[medianIndex];

        right->keys.assign(full->keys.begin() + degree, full->keys.end());
        full->keys.resize(medianIndex);

        if (!full->leaf) {
            right->child.assign(full->child.begin() + degree, full->child.end());
            full->child.resize(degree);
        }

        parent->keys.insert(parent->keys.begin() + childIndex, median);
        parent->child.insert(parent->child.begin() + childIndex + 1, right);
    }

    void insertNonFull(Node* node, int key) {
        if (node->leaf) {
            node->keys.insert(
                std::upper_bound(node->keys.begin(), node->keys.end(), key),
                key
            );
            return;
        }

        int pos = lowerBoundIndex(node, key);

        if (static_cast<int>(node->child[pos]->keys.size()) == (2 * degree - 1)) {
            splitChild(node, pos);

            if (key > node->keys[pos]) {
                ++pos;
            }
        }

        insertNonFull(node->child[pos], key);
    }

    void inorder(const Node* node) const {
        for (int i = 0; i < static_cast<int>(node->keys.size()); ++i) {
            if (!node->leaf) {
                inorder(node->child[i]);
            }
            std::cout << node->keys[i] << ' ';
        }

        if (!node->leaf) {
            inorder(node->child.back());
        }
    }

    int predecessor(Node* node, int keyIndex) const {
        Node* cursor = node->child[keyIndex];
        while (!cursor->leaf) {
            cursor = cursor->child.back();
        }
        return cursor->keys.back();
    }

    int successor(Node* node, int keyIndex) const {
        Node* cursor = node->child[keyIndex + 1];
        while (!cursor->leaf) {
            cursor = cursor->child.front();
        }
        return cursor->keys.front();
    }

    void mergeChildren(Node* parent, int leftIndex) {
        Node* left = parent->child[leftIndex];
        Node* right = parent->child[leftIndex + 1];

        left->keys.push_back(parent->keys[leftIndex]);
        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());

        if (!left->leaf) {
            left->child.insert(left->child.end(), right->child.begin(), right->child.end());
        }

        parent->keys.erase(parent->keys.begin() + leftIndex);
        parent->child.erase(parent->child.begin() + leftIndex + 1);

        delete right;
    }

    void borrowFromLeft(Node* parent, int childIndex) {
        Node* childNode = parent->child[childIndex];
        Node* sibling = parent->child[childIndex - 1];

        childNode->keys.insert(childNode->keys.begin(), parent->keys[childIndex - 1]);
        parent->keys[childIndex - 1] = sibling->keys.back();
        sibling->keys.pop_back();

        if (!sibling->leaf) {
            childNode->child.insert(childNode->child.begin(), sibling->child.back());
            sibling->child.pop_back();
        }
    }

    void borrowFromRight(Node* parent, int childIndex) {
        Node* childNode = parent->child[childIndex];
        Node* sibling = parent->child[childIndex + 1];

        childNode->keys.push_back(parent->keys[childIndex]);
        parent->keys[childIndex] = sibling->keys.front();
        sibling->keys.erase(sibling->keys.begin());

        if (!sibling->leaf) {
            childNode->child.push_back(sibling->child.front());
            sibling->child.erase(sibling->child.begin());
        }
    }

    void prepareChild(Node* parent, int childIndex) {
        if (childIndex > 0 && static_cast<int>(parent->child[childIndex - 1]->keys.size()) >= degree) {
            borrowFromLeft(parent, childIndex);
            return;
        }

        if (childIndex < static_cast<int>(parent->keys.size()) &&
            static_cast<int>(parent->child[childIndex + 1]->keys.size()) >= degree) {
            borrowFromRight(parent, childIndex);
            return;
        }

        if (childIndex < static_cast<int>(parent->keys.size())) {
            mergeChildren(parent, childIndex);
        } else {
            mergeChildren(parent, childIndex - 1);
        }
    }

    void removeFromInternal(Node* node, int keyIndex) {
        const int key = node->keys[keyIndex];

        if (static_cast<int>(node->child[keyIndex]->keys.size()) >= degree) {
            const int pred = predecessor(node, keyIndex);
            node->keys[keyIndex] = pred;
            removeKey(node->child[keyIndex], pred);
            return;
        }

        if (static_cast<int>(node->child[keyIndex + 1]->keys.size()) >= degree) {
            const int succ = successor(node, keyIndex);
            node->keys[keyIndex] = succ;
            removeKey(node->child[keyIndex + 1], succ);
            return;
        }

        mergeChildren(node, keyIndex);
        removeKey(node->child[keyIndex], key);
    }

    void removeKey(Node* node, int key) {
        int pos = lowerBoundIndex(node, key);

        if (pos < static_cast<int>(node->keys.size()) && node->keys[pos] == key) {
            if (node->leaf) {
                node->keys.erase(node->keys.begin() + pos);
            } else {
                removeFromInternal(node, pos);
            }
            return;
        }

        if (node->leaf) {
            return;
        }

        const bool descendedPastLastKey = (pos == static_cast<int>(node->keys.size()));

        if (static_cast<int>(node->child[pos]->keys.size()) == degree - 1) {
            prepareChild(node, pos);
        }

        if (descendedPastLastKey && pos > static_cast<int>(node->keys.size())) {
            removeKey(node->child[pos - 1], key);
        } else {
            removeKey(node->child[pos], key);
        }
    }

public:
    explicit BTree(int minDegree) : root(new Node(true)), degree(minDegree) {}

    ~BTree() {
        destroy(root);
    }

    BTree(const BTree&) = delete;
    BTree& operator=(const BTree&) = delete;

    void insert(int key) {
        if (contains(key)) {
            std::cout << "Duplicate key skipped.\n";
            return;
        }

        if (static_cast<int>(root->keys.size()) == (2 * degree - 1)) {
            Node* newRoot = new Node(false);
            newRoot->child.push_back(root);
            splitChild(newRoot, 0);
            root = newRoot;
        }

        insertNonFull(root, key);
    }

    void erase(int key) {
        if (root->keys.empty()) {
            return;
        }

        removeKey(root, key);

        if (root->keys.empty() && !root->leaf) {
            Node* oldRoot = root;
            root = root->child.front();
            oldRoot->child.clear();
            delete oldRoot;
        }
    }

    bool contains(int key) const {
        return !root->keys.empty() && contains(root, key);
    }

    void printInorder() const {
        if (root->keys.empty()) {
            std::cout << "(empty)";
        } else {
            inorder(root);
        }
        std::cout << '\n';
    }

    void printLevels() const {
        if (root->keys.empty()) {
            std::cout << "(empty tree)\n";
            return;
        }

        std::queue<std::pair<const Node*, int>> pending;
        pending.push({root, 0});

        int printedLevel = -1;
        while (!pending.empty()) {
            const Node* node = pending.front().first;
            const int level = pending.front().second;
            pending.pop();

            if (level != printedLevel) {
                if (printedLevel != -1) {
                    std::cout << '\n';
                }
                std::cout << "Level " << level << ": ";
                printedLevel = level;
            }

            std::cout << '[';
            for (std::size_t i = 0; i < node->keys.size(); ++i) {
                if (i != 0) {
                    std::cout << ' ';
                }
                std::cout << node->keys[i];
            }
            std::cout << "] ";

            for (const Node* next : node->child) {
                pending.push({next, level + 1});
            }
        }
        std::cout << '\n';
    }
};

int main() {
    int degree = 0;
    std::cout << "Enter minimum degree (t >= 2): ";
    std::cin >> degree;

    if (!std::cin || degree < 2) {
        std::cout << "Minimum degree must be at least 2.\n";
        return 1;
    }

    BTree tree(degree);

    while (true) {
        std::cout << "\n1. Insert\n"
                  << "2. Delete\n"
                  << "3. Search\n"
                  << "4. Print inorder\n"
                  << "5. Print levels\n"
                  << "6. Exit\n"
                  << "Choice: ";

        int choice = 0;
        std::cin >> choice;

        if (!std::cin) {
            std::cout << "Input ended.\n";
            break;
        }

        if (choice == 6) {
            break;
        }

        int key = 0;
        switch (choice) {
            case 1:
                std::cout << "Key: ";
                std::cin >> key;
                tree.insert(key);
                break;
            case 2:
                std::cout << "Key: ";
                std::cin >> key;
                tree.erase(key);
                break;
            case 3:
                std::cout << "Key: ";
                std::cin >> key;
                std::cout << (tree.contains(key) ? "Found\n" : "Not found\n");
                break;
            case 4:
                tree.printInorder();
                break;
            case 5:
                tree.printLevels();
                break;
            default:
                std::cout << "Choose a valid menu option.\n";
                break;
        }
    }

    return 0;
}
