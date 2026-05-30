#include "btree.hpp"

TreeNode::TreeNode(int deg, bool leaf) {
    degree = deg;
    isLeaf = leaf;
    keyCount = 0;

    data = new int[2 * degree - 1];
    children = new TreeNode*[2 * degree];

    for (int i = 0; i < 2 * degree; i++) {
        children[i] = nullptr;
    }
}

TreeNode::~TreeNode() {
    if (!isLeaf) {
        for (int i = 0; i <= keyCount; i++) {
            delete children[i];
        }
    }
    delete[] data;
    delete[] children;
}

void TreeNode::inorderTraversal() {
    int i;
    for (i = 0; i < keyCount; i++) {
        if (!isLeaf && children[i] != nullptr) {
            children[i]->inorderTraversal();
        }
        std::cout << data[i] << " ";
    }

    if (!isLeaf && children[i] != nullptr) {
        children[i]->inorderTraversal();
    }
}

TreeNode* TreeNode::searchKey(int value, int& pos) {
    int i = 0;

    while (i < keyCount && value > data[i]) {
        i++;
    }

    if (i < keyCount && data[i] == value) {
        pos = i;
        return this;
    }

    if (isLeaf) {
        return nullptr;
    }

    return children[i]->searchKey(value, pos);
}

void TreeNode::insertInside(int value) {
    int i = keyCount - 1;

    if (isLeaf) {
        while (i >= 0 && data[i] > value) {
            data[i + 1] = data[i];
            i--;
        }

        data[i + 1] = value;
        keyCount++;
    } else {
        while (i >= 0 && data[i] > value) {
            i--;
        }

        if (children[i + 1]->keyCount == 2 * degree - 1) {
            divideChild(i + 1, children[i + 1]);

            if (value > data[i + 1]) {
                i++;
            }
        }

        children[i + 1]->insertInside(value);
    }
}

void TreeNode::divideChild(int idx, TreeNode* node) {
    TreeNode* newNode = new TreeNode(node->degree, node->isLeaf);
    newNode->keyCount = degree - 1;

    for (int i = 0; i < degree - 1; i++) {
        newNode->data[i] = node->data[i + degree];
    }

    if (!node->isLeaf) {
        for (int i = 0; i < degree; i++) {
            newNode->children[i] = node->children[i + degree];
        }
    }

    node->keyCount = degree - 1;

    for (int i = keyCount; i >= idx + 1; i--) {
        children[i + 1] = children[i];
    }

    children[idx + 1] = newNode;

    for (int i = keyCount - 1; i >= idx; i--) {
        data[i + 1] = data[i];
    }

    data[idx] = node->data[degree - 1];
    keyCount++;
}

void TreeNode::displayTree(const std::string& space) {
    std::cout << "{ ";
    for (int i = 0; i < keyCount; i++) {
        std::cout << data[i] << " ";
    }
    std::cout << "}\\n";

    if (!isLeaf) {
        for (int i = 0; i <= keyCount; i++) {
            if (children[i] != nullptr) {
                std::cout << space;
                std::cout << (i == keyCount ? "└── " : "├── ");
                children[i]->displayTree(space + "   ");
            }
        }
    }
}

BTree::BTree(int degree) {
    root = nullptr;
    minDegree = degree;
}

BTree::~BTree() {
    delete root;
}

void BTree::insert(int value) {
    if (root == nullptr) {
        root = new TreeNode(minDegree, true);
        root->data[0] = value;
        root->keyCount = 1;
        return;
    }

    if (root->keyCount == 2 * minDegree - 1) {
        TreeNode* temp = new TreeNode(minDegree, false);
        temp->children[0] = root;
        temp->divideChild(0, root);

        int index = 0;
        if (value > temp->data[0]) {
            index++;
        }

        temp->children[index]->insertInside(value);
        root = temp;
    } else {
        root->insertInside(value);
    }
}

TreeNode* BTree::search(int value, int& position) {
    if (root == nullptr) {
        return nullptr;
    }
    return root->searchKey(value, position);
}

void BTree::traverse() {
    if (root != nullptr) {
        root->inorderTraversal();
    }
}

void BTree::printTree() {
    if (root == nullptr) {
        std::cout << "Tree is Empty\\n";
        return;
    }
    root->displayTree("");
}