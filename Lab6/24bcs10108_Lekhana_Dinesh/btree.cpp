#include <algorithm>
#include <iostream>
#include <queue>
#include <vector>

class BTreeNode {
public:
    bool leaf;
    std::vector<int> keys;
    std::vector<BTreeNode*> children;

    BTreeNode(int t, bool leafNode)
        : leaf(leafNode), keys(), children() {
        keys.reserve(2 * t - 1);
        children.reserve(2 * t);
    }

    ~BTreeNode() {
        for (BTreeNode* child : children) {
            delete child;
        }
    }
};

class BTree {
public:
    BTree(int degree)
        : t(degree), root(nullptr) {
        if (t < 2) {
            throw std::invalid_argument("Minimum degree must be at least 2");
        }
    }

    ~BTree() {
        delete root;
    }

    void insert(int key) {
        if (!root) {
            root = new BTreeNode(t, true);
            root->keys.push_back(key);
            return;
        }

        if (contains(key)) {
            std::cout << "Duplicate key " << key << " ignored." << std::endl;
            return;
        }

        if (static_cast<int>(root->keys.size()) == 2 * t - 1) {
            BTreeNode* oldRoot = root;
            root = new BTreeNode(t, false);
            root->children.push_back(oldRoot);
            splitChild(root, 0);
            insertNonFull(root, key);
        } else {
            insertNonFull(root, key);
        }
    }

    bool contains(int key) const {
        return search(root, key) != nullptr;
    }

    void inorderTraversal() const {
        std::cout << "Inorder traversal: ";
        inorder(root);
        std::cout << std::endl;
    }

    void displayTree() const {
        if (!root) {
            std::cout << "B-Tree is empty." << std::endl;
            return;
        }

        std::queue<BTreeNode*> nodes;
        nodes.push(root);

        int level = 0;
        while (!nodes.empty()) {
            int count = static_cast<int>(nodes.size());
            std::cout << "Level " << level << ": ";
            while (count--) {
                BTreeNode* node = nodes.front();
                nodes.pop();
                std::cout << "[";
                for (size_t i = 0; i < node->keys.size(); ++i) {
                    std::cout << node->keys[i];
                    if (i + 1 < node->keys.size()) {
                        std::cout << ",";
                    }
                }
                std::cout << "] ";
                if (!node->leaf) {
                    for (BTreeNode* child : node->children) {
                        nodes.push(child);
                    }
                }
            }
            std::cout << std::endl;
            ++level;
        }
    }

private:
    int t;
    BTreeNode* root;

    BTreeNode* search(BTreeNode* node, int key) const {
        if (!node) {
            return nullptr;
        }

        int index = 0;
        while (index < static_cast<int>(node->keys.size()) && key > node->keys[index]) {
            ++index;
        }

        if (index < static_cast<int>(node->keys.size()) && node->keys[index] == key) {
            return node;
        }

        if (node->leaf) {
            return nullptr;
        }

        return search(node->children[index], key);
    }

    void inorder(BTreeNode* node) const {
        if (!node) {
            return;
        }

        for (int i = 0; i < static_cast<int>(node->keys.size()); ++i) {
            if (!node->leaf) {
                inorder(node->children[i]);
            }
            std::cout << node->keys[i] << " ";
        }

        if (!node->leaf) {
            inorder(node->children[node->keys.size()]);
        }
    }

    void splitChild(BTreeNode* parent, int index) {
        BTreeNode* child = parent->children[index];
        BTreeNode* sibling = new BTreeNode(t, child->leaf);

        int mid = t - 1;
        for (int j = 0; j < mid; ++j) {
            sibling->keys.push_back(child->keys[j + t]);
        }

        if (!child->leaf) {
            for (int j = 0; j < t; ++j) {
                sibling->children.push_back(child->children[j + t]);
            }
        }

        int median = child->keys[mid];
        child->keys.resize(mid);
        if (!child->leaf) {
            child->children.resize(t);
        }

        parent->children.insert(parent->children.begin() + index + 1, sibling);
        parent->keys.insert(parent->keys.begin() + index, median);
    }

    void insertNonFull(BTreeNode* node, int key) {
        int position = static_cast<int>(node->keys.size()) - 1;

        if (node->leaf) {
            node->keys.push_back(0);
            while (position >= 0 && node->keys[position] > key) {
                node->keys[position + 1] = node->keys[position];
                --position;
            }
            node->keys[position + 1] = key;
            return;
        }

        while (position >= 0 && node->keys[position] > key) {
            --position;
        }
        ++position;

        if (static_cast<int>(node->children[position]->keys.size()) == 2 * t - 1) {
            splitChild(node, position);
            if (node->keys[position] < key) {
                ++position;
            }
        }
        insertNonFull(node->children[position], key);
    }
};

int main() {
    BTree tree(3);
    std::vector<int> values = { 12, 5, 18, 3, 7, 15, 20, 30, 10, 8, 25 };

    std::cout << "Inserting keys:";
    for (int value : values) {
        std::cout << " " << value;
        tree.insert(value);
    }
    std::cout << std::endl << std::endl;

    tree.inorderTraversal();
    std::cout << std::endl;

    std::cout << "B-Tree level-order structure:" << std::endl;
    tree.displayTree();
    std::cout << std::endl;

    int presentKey = 15;
    int missingKey = 99;
    std::cout << "Search for " << presentKey << ": " << (tree.contains(presentKey) ? "Found" : "Not found") << std::endl;
    std::cout << "Search for " << missingKey << ": " << (tree.contains(missingKey) ? "Found" : "Not found") << std::endl;

    return 0;
}
