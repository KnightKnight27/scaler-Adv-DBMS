#include <iostream>
#include <vector>
#include <string>

template <typename K, typename V>
class Database {
private:
    struct Record {
        K id;
        V value;
    };

    struct Node {
        bool leafNode;
        std::vector<Record> keys;
        std::vector<Node*> next;

        explicit Node(bool leaf) : leafNode(leaf) {}
    };

    Node* head;
    size_t degree;

    void split(Node* parent, size_t childPos) {
        Node* child = parent->next[childPos];
        Node* sibling = new Node(child->leafNode);

        size_t t = degree;

        Record promoted = child->keys[t - 1];

        for (size_t i = t; i < child->keys.size(); ++i) {
            sibling->keys.push_back(child->keys[i]);
        }

        if (!child->leafNode) {
            for (size_t i = t; i < child->next.size(); ++i) {
                sibling->next.push_back(child->next[i]);
            }
        }

        child->keys.resize(t - 1);

        if (!child->leafNode) {
            child->next.resize(t);
        }

        parent->keys.insert(parent->keys.begin() + childPos, promoted);
        parent->next.insert(parent->next.begin() + childPos + 1, sibling);
    }

    void insertIntoNode(Node* current, const K& key, const V& value) {
        if (current->leafNode) {
            Record rec{key, value};

            auto pos = current->keys.begin();

            while (pos != current->keys.end() && pos->id < key) {
                ++pos;
            }

            current->keys.insert(pos, rec);
            return;
        }

        size_t childIndex = 0;

        while (childIndex < current->keys.size() &&
               key > current->keys[childIndex].id) {
            ++childIndex;
        }

        if (current->next[childIndex]->keys.size() == 2 * degree - 1) {
            split(current, childIndex);

            if (key > current->keys[childIndex].id) {
                ++childIndex;
            }
        }

        insertIntoNode(current->next[childIndex], key, value);
    }

public:
    explicit Database(size_t t)
        : head(new Node(true)), degree(t) {}

    void insert(const K& key, const V& value) {
        if (head->keys.size() == 2 * degree - 1) {
            Node* newRoot = new Node(false);

            newRoot->next.push_back(head);
            split(newRoot, 0);

            head = newRoot;
        }

        insertIntoNode(head, key, value);
    }

    bool find(const K& key, V& result) const {
        Node* cursor = head;

        while (cursor) {
            size_t pos = 0;

            while (pos < cursor->keys.size() &&
                   key > cursor->keys[pos].id) {
                ++pos;
            }

            if (pos < cursor->keys.size() &&
                cursor->keys[pos].id == key) {
                result = cursor->keys[pos].value;
                return true;
            }

            if (cursor->leafNode) {
                return false;
            }

            cursor = cursor->next[pos];
        }

        return false;
    }
};

int main() {
    Database<int, std::string> store(3);

    store.insert(10, "A");
    store.insert(20, "B");
    store.insert(5, "C");

    std::string answer;

    if (store.find(20, answer)) {
        std::cout << answer << '\n';
    }

    return 0;
}