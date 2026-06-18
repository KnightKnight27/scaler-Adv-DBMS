#include <iostream>
#include <string>

template<typename Key, typename Row>
class RBTree {
public:
    struct Node {
        Key key;
        Row row;
        Node* left;
        Node* right;
        bool isRed;

        Node(Key k, Row r) : key(k), row(r), left(nullptr), right(nullptr), isRed(true) {}
    };

private:
    Node* root = nullptr;

    bool IsRed(Node* node) {
        if (!node) return false;
        return node->isRed;
    }

    Node* RotateLeft(Node* h) {
        Node* x = h->right;
        h->right = x->left;
        x->left = h;
        x->isRed = h->isRed;
        h->isRed = true;
        return x;
    }

    Node* RotateRight(Node* h) {
        Node* x = h->left;
        h->left = x->right;
        x->right = h;
        x->isRed = h->isRed;
        h->isRed = true;
        return x;
    }

    void FlipColors(Node* h) {
        h->isRed = true;
        h->left->isRed = false;
        h->right->isRed = false;
    }

    Node* InsertHelper(Node* h, Key key, Row row) {
        if (!h) return new Node(key, row);

        if (key < h->key)      h->left = InsertHelper(h->left, key, row);
        else if (key > h->key) h->right = InsertHelper(h->right, key, row);
        else                   h->row = row;

        if (IsRed(h->right) && !IsRed(h->left))    h = RotateLeft(h);
        if (IsRed(h->left) && IsRed(h->left->left)) h = RotateRight(h);
        if (IsRed(h->left) && IsRed(h->right))     FlipColors(h);

        return h;
    }

public:
    void Insert(Key key, Row row) {
        root = InsertHelper(root, key, row);
        root->isRed = false;
    }

    Node* Search(Key key) {
        Node* current = root;
        while (current) {
            if (key < current->key)       current = current->left;
            else if (key > current->key)  current = current->right;
            else                          return current;
        }
        return nullptr;
    }
};

int main() {
    RBTree<int, std::string> tree;
    tree.Insert(10, "Apple");
    tree.Insert(20, "Banana");
    tree.Insert(5, "Cherry");

    auto res = tree.Search(20);
    if (res) std::cout << res->row << "\n";
    return 0;
}