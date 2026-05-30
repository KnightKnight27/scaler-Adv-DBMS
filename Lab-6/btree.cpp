#include <iostream>
#include <vector>
#include <algorithm>

template<typename T>
class BTreeNode {
public:
    std::vector<T> keys;
    std::vector<BTreeNode*> children;

    bool leaf;
    int t;

    BTreeNode(int degree, bool isLeaf)
        : t(degree), leaf(isLeaf) {}

    void traverse() {
        int i;

        for (i = 0; i < keys.size(); i++) {

            if (!leaf)
                children[i]->traverse();

            std::cout << keys[i] << " ";
        }

        if (!leaf)
            children[i]->traverse();
    }

    BTreeNode* search(T key) {

        int i = 0;

        while (i < keys.size() && key > keys[i])
            i++;

        if (i < keys.size() && keys[i] == key)
            return this;

        if (leaf)
            return nullptr;

        return children[i]->search(key);
    }
};

template<typename T>
class BTree {

private:

    BTreeNode<T>* root;
    int t;

    void splitChild(BTreeNode<T>* parent, int index) {

        BTreeNode<T>* fullChild = parent->children[index];

        BTreeNode<T>* newNode =
            new BTreeNode<T>(t, fullChild->leaf);

        T middleKey = fullChild->keys[t - 1];

        for (int i = t; i < fullChild->keys.size(); i++)
            newNode->keys.push_back(fullChild->keys[i]);

        if (!fullChild->leaf) {

            for (int i = t; i < fullChild->children.size(); i++)
                newNode->children.push_back(
                    fullChild->children[i]);

            fullChild->children.resize(t);
        }

        fullChild->keys.resize(t - 1);

        parent->children.insert(
            parent->children.begin() + index + 1,
            newNode
        );

        parent->keys.insert(
            parent->keys.begin() + index,
            middleKey
        );
    }

    void insertNonFull(BTreeNode<T>* node, T key) {

        int i = node->keys.size() - 1;

        if (node->leaf) {

            node->keys.push_back(key);

            while (i >= 0 && node->keys[i] > key) {

                node->keys[i + 1] =
                    node->keys[i];

                i--;
            }

            node->keys[i + 1] = key;
        }
        else {

            while (i >= 0 &&
                   key < node->keys[i])
                i--;

            i++;

            if (node->children[i]->keys.size()
                == 2 * t - 1) {

                splitChild(node, i);

                if (key > node->keys[i])
                    i++;
            }

            insertNonFull(
                node->children[i],
                key
            );
        }
    }

public:

    explicit BTree(int degree) {

        root = nullptr;
        t = degree;
    }

    void insert(T key) {

        if (root == nullptr) {

            root =
                new BTreeNode<T>(t, true);

            root->keys.push_back(key);

            return;
        }

        if (root->keys.size()
            == 2 * t - 1) {

            BTreeNode<T>* newRoot =
                new BTreeNode<T>(t, false);

            newRoot->children.push_back(root);

            splitChild(newRoot, 0);

            int i = 0;

            if (key > newRoot->keys[0])
                i++;

            insertNonFull(
                newRoot->children[i],
                key
            );

            root = newRoot;
        }
        else {

            insertNonFull(root, key);
        }
    }

    bool contains(T key) {

        if (!root)
            return false;

        return root->search(key) != nullptr;
    }

    void printInorder() {

        if (root)
            root->traverse();

        std::cout << "\n";
    }
};

int main() {

    BTree<int> tree(3);

    std::vector<int> values =
    {
        10, 20, 5, 6, 12,
        30, 7, 17, 25, 40
    };

    for (int x : values)
        tree.insert(x);

    std::cout
        << "Inorder Traversal:\n";

    tree.printInorder();

    std::cout
        << "Search 12: "
        << (tree.contains(12)
            ? "Found"
            : "Not Found")
        << "\n";

    std::cout
        << "Search 50: "
        << (tree.contains(50)
            ? "Found"
            : "Not Found")
        << "\n";

    return 0;
}