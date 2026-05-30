#include <algorithm>
#include <iostream>
#include <memory>
#include <queue>
#include <vector>

// Lab 6: B+ Tree Implementation
// Name: Ankit Kumar
// Roll No: 24BCS10189

class BPlusTree {
private:
    struct Node {
        bool leaf;
        std::vector<int> keys;
        std::vector<std::unique_ptr<Node>> children;
        Node* next{nullptr};

        explicit Node(bool isLeaf) : leaf(isLeaf) {}
    };

    static constexpr int order = 4;
    static constexpr int maxKeys = order - 1;

    std::unique_ptr<Node> root;

public:
    BPlusTree() : root(std::make_unique<Node>(true)) {}

    void insert(int key) {
        if (root->keys.size() == maxKeys) {
            auto newRoot = std::make_unique<Node>(false);
            newRoot->children.push_back(std::move(root));
            splitChild(newRoot.get(), 0);
            root = std::move(newRoot);
        }

        insertNonFull(root.get(), key);
    }

    bool search(int key) const {
        const Node* leaf = findLeaf(key);
        return std::binary_search(leaf->keys.begin(), leaf->keys.end(), key);
    }

    void printLevelOrder() const {
        std::queue<const Node*> q;
        q.push(root.get());

        while (!q.empty()) {
            int levelSize = static_cast<int>(q.size());
            while (levelSize-- > 0) {
                const Node* node = q.front();
                q.pop();

                std::cout << "[";
                for (std::size_t i = 0; i < node->keys.size(); ++i) {
                    std::cout << node->keys[i];
                    if (i + 1 < node->keys.size()) {
                        std::cout << " ";
                    }
                }
                std::cout << "] ";

                if (!node->leaf) {
                    for (const auto& child : node->children) {
                        q.push(child.get());
                    }
                }
            }
            std::cout << '\n';
        }
    }

    void printLeaves() const {
        const Node* node = root.get();
        while (!node->leaf) {
            node = node->children.front().get();
        }

        while (node != nullptr) {
            std::cout << "[";
            for (std::size_t i = 0; i < node->keys.size(); ++i) {
                std::cout << node->keys[i];
                if (i + 1 < node->keys.size()) {
                    std::cout << " ";
                }
            }
            std::cout << "] -> ";
            node = node->next;
        }
        std::cout << "NULL\n";
    }

private:
    void splitChild(Node* parent, std::size_t index) {
        Node* child = parent->children[index].get();
        auto sibling = std::make_unique<Node>(child->leaf);

        if (child->leaf) {
            const std::size_t split = (child->keys.size() + 1) / 2;
            sibling->keys.assign(child->keys.begin() + split, child->keys.end());
            child->keys.erase(child->keys.begin() + split, child->keys.end());

            sibling->next = child->next;
            child->next = sibling.get();

            int promotedKey = sibling->keys.front();
            parent->keys.insert(parent->keys.begin() + index, promotedKey);
        } else {
            const std::size_t mid = child->keys.size() / 2;
            int promotedKey = child->keys[mid];

            sibling->keys.assign(child->keys.begin() + mid + 1, child->keys.end());
            child->keys.erase(child->keys.begin() + mid, child->keys.end());

            auto childMoveBegin = std::make_move_iterator(child->children.begin() + mid + 1);
            auto childMoveEnd = std::make_move_iterator(child->children.end());
            sibling->children.assign(childMoveBegin, childMoveEnd);
            child->children.erase(child->children.begin() + mid + 1, child->children.end());

            parent->keys.insert(parent->keys.begin() + index, promotedKey);
        }

        parent->children.insert(parent->children.begin() + index + 1, std::move(sibling));
    }

    void insertNonFull(Node* node, int key) {
        if (node->leaf) {
            auto pos = std::lower_bound(node->keys.begin(), node->keys.end(), key);
            if (pos == node->keys.end() || *pos != key) {
                node->keys.insert(pos, key);
            }
            return;
        }

        std::size_t index = std::upper_bound(node->keys.begin(), node->keys.end(), key) - node->keys.begin();
        if (node->children[index]->keys.size() == maxKeys) {
            splitChild(node, index);
            if (key >= node->keys[index]) {
                ++index;
            }
        }

        insertNonFull(node->children[index].get(), key);
    }

    const Node* findLeaf(int key) const {
        const Node* node = root.get();
        while (!node->leaf) {
            std::size_t index = std::upper_bound(node->keys.begin(), node->keys.end(), key) - node->keys.begin();
            node = node->children[index].get();
        }
        return node;
    }
};

int main() {
    BPlusTree tree;
    std::vector<int> keys{10, 20, 5, 6, 12, 30, 7, 17, 3, 25, 40, 50};

    for (int key : keys) {
        tree.insert(key);
    }

    std::cout << "B+ Tree level order:\n";
    tree.printLevelOrder();

    std::cout << "Leaf chain:\n";
    tree.printLeaves();

    std::cout << "Search 17: " << (tree.search(17) ? "found" : "not found") << '\n';
    std::cout << "Search 99: " << (tree.search(99) ? "found" : "not found") << '\n';

    return 0;
}
