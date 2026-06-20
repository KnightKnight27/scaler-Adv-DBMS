#include <iostream>
#include <vector>

const int T = 2;  // Minimum degree: each node can contain at most 2T-1 keys.

struct Node {
    bool leaf;
    std::vector<int> keys;
    std::vector<Node*> children;

    explicit Node(bool isLeaf) : leaf(isLeaf) {}
};

class BTree {
private:
    Node* root = nullptr;

    void destroy(Node* node) {
        if(node == nullptr) return;
        for (Node* child : node->children) destroy(child);
        delete node;
    }

    bool search(Node* node, int key) const {
        int index = 0;
        while(index < static_cast<int>(node->keys.size()) && key > node->keys[index]) {
            ++index;
        }

        if(index < static_cast<int>(node->keys.size()) &&
            node->keys[index] == key) {
            return true;
        }

        if(node->leaf) return false;
        return search(node->children[index], key);
    }

    void splitChild(Node* parent, int index) {
        Node* fullChild = parent->children[index];
        Node* rightChild = new Node(fullChild->leaf);
        int median = fullChild->keys[T - 1];

        rightChild->keys.assign(fullChild->keys.begin() + T,
                                fullChild->keys.end());
        fullChild->keys.resize(T - 1);

        if(!fullChild->leaf) {
            rightChild->children.assign(fullChild->children.begin() + T,
                                        fullChild->children.end());
            fullChild->children.resize(T);
        }

        parent->keys.insert(parent->keys.begin() + index, median);
        parent->children.insert(parent->children.begin() + index + 1,
                                rightChild);
    }

    void insertNonFull(Node* node, int key) {
        int index = static_cast<int>(node->keys.size()) - 1;

        if(node->leaf) {
            node->keys.push_back(0);
            while(index >= 0 && key < node->keys[index]) {
                node->keys[index + 1] = node->keys[index];
                --index;
            }
            node->keys[index + 1] = key;
            return;
        }

        while(index >= 0 && key < node->keys[index]) --index;
        ++index;

        if(static_cast<int>(node->children[index]->keys.size()) == 2 * T - 1) {
            splitChild(node, index);
            if(key > node->keys[index]) ++index;
        }

        insertNonFull(node->children[index], key);
    }

    int findKey(Node* node, int key) const {
        int index = 0;
        while(index < static_cast<int>(node->keys.size()) && node->keys[index] < key) {
            ++index;
        }
        return index;
    }

    int predecessor(Node* node, int index) const {
        Node* current = node->children[index];
        while(!current->leaf) current = current->children.back();
        return current->keys.back();
    }

    int successor(Node* node, int index) const {
        Node* current = node->children[index + 1];
        while(!current->leaf) current = current->children.front();
        return current->keys.front();
    }

    void borrowFromPrevious(Node* node, int index) {
        Node* child = node->children[index];
        Node* sibling = node->children[index - 1];

        child->keys.insert(child->keys.begin(), node->keys[index - 1]);
        node->keys[index - 1] = sibling->keys.back();
        sibling->keys.pop_back();

        if(!child->leaf) {
            child->children.insert(child->children.begin(),
                                   sibling->children.back());
            sibling->children.pop_back();
        }
    }

    void borrowFromNext(Node* node, int index) {
        Node* child = node->children[index];
        Node* sibling = node->children[index + 1];

        child->keys.push_back(node->keys[index]);
        node->keys[index] = sibling->keys.front();
        sibling->keys.erase(sibling->keys.begin());

        if(!child->leaf) {
            child->children.push_back(sibling->children.front());
            sibling->children.erase(sibling->children.begin());
        }
    }

    void merge(Node* node, int index) {
        Node* leftChild = node->children[index];
        Node* rightChild = node->children[index + 1];

        leftChild->keys.push_back(node->keys[index]);
        leftChild->keys.insert(leftChild->keys.end(),
                               rightChild->keys.begin(), rightChild->keys.end());

        if(!leftChild->leaf) {
            leftChild->children.insert(leftChild->children.end(),
                                       rightChild->children.begin(),
                                       rightChild->children.end());
        }

        node->keys.erase(node->keys.begin() + index);
        node->children.erase(node->children.begin() + index + 1);
        delete rightChild;
    }

    // Make sure child[index] has at least T keys before descending into it.
    void fill(Node* node, int index) {
        if(index > 0 &&
            static_cast<int>(node->children[index - 1]->keys.size()) >= T) {
            borrowFromPrevious(node, index);
        } else if(index < static_cast<int>(node->children.size()) - 1 &&
                   static_cast<int>(node->children[index + 1]->keys.size()) >= T) {
            borrowFromNext(node, index);
        } else if(index < static_cast<int>(node->children.size()) - 1) {
            merge(node, index);
        } else {
            merge(node, index - 1);
        }
    }

    void removeFromInternal(Node* node, int index) {
        int key = node->keys[index];

        if(static_cast<int>(node->children[index]->keys.size()) >= T) {
            int replacement = predecessor(node, index);
            node->keys[index] = replacement;
            remove(node->children[index], replacement);
        } else if(static_cast<int>(node->children[index + 1]->keys.size()) >= T) {
            int replacement = successor(node, index);
            node->keys[index] = replacement;
            remove(node->children[index + 1], replacement);
        } else {
            merge(node, index);
            remove(node->children[index], key);
        }
    }

    void remove(Node* node, int key) {
        int index = findKey(node, key);

        if(index < static_cast<int>(node->keys.size()) &&
            node->keys[index] == key) {
            if(node->leaf) {
                node->keys.erase(node->keys.begin() + index);
            } else {
                removeFromInternal(node, index);
            }
            return;
        }

        if(node->leaf) return;

        bool lastChild = (index == static_cast<int>(node->keys.size()));

        if(static_cast<int>(node->children[index]->keys.size()) < T) {
            fill(node, index);
        }

        if(lastChild && index > static_cast<int>(node->keys.size())) {
            remove(node->children[index - 1], key);
        } else {
            remove(node->children[index], key);
        }
    }

    void inorder(Node* node) const {
        if(node == nullptr) return;

        for (int index = 0; index < static_cast<int>(node->keys.size()); ++index) {
            if(!node->leaf) inorder(node->children[index]);
            std::cout << node->keys[index] << ' ';
        }

        if(!node->leaf) inorder(node->children.back());
    }

public:
    ~BTree() {
        destroy(root);
    }

    bool search(int key) const {
        return root != nullptr && search(root, key);
    }

    bool insert(int key) {
        if(search(key)) return false;  // Ignore duplicate keys.

        if(root == nullptr) {
            root = new Node(true);
            root->keys.push_back(key);
            return true;
        }

        if(static_cast<int>(root->keys.size()) == 2 * T - 1) {
            Node* newRoot = new Node(false);
            newRoot->children.push_back(root);
            splitChild(newRoot, 0);
            root = newRoot;
        }

        insertNonFull(root, key);
        return true;
    }

    bool remove(int key) {
        if(!search(key)) return false;

        remove(root, key);

        if(root->keys.empty()) {
            Node* oldRoot = root;
            root = root->leaf ? nullptr : root->children.front();
            delete oldRoot;
        }

        return true;
    }

    void print() const {
        inorder(root);
        std::cout << '\n';
    }
};

int main() {
    BTree tree;

    for (int key : {10, 20, 5, 6, 12, 30, 7, 17, 3, 1, 25}) {
        tree.insert(key);
    }

    std::cout << "After insertion: ";
    tree.print();

    std::cout << "Search 17: "
              << (tree.search(17) ? "Found" : "Not found") << '\n';
    std::cout << "Search 99: "
              << (tree.search(99) ? "Found" : "Not found") << '\n';

    tree.remove(6);
    tree.remove(20);
    tree.remove(5);

    std::cout << "After deletion: ";
    tree.print();

    return 0;
}
