#include "BTree.hpp"


// BTreeNode Implementation

// Initialize node with minimum degree (t1) and leaf status
BTreeNode::BTreeNode(int t1, bool leaf1) : t(t1), leaf(leaf1) {}

// Print all keys in the subtree rooted at this node
void BTreeNode::traverse() {
    int i;
    for (i = 0; i < keys.size(); i++) {
        if (!leaf) {
            children[i]->traverse();
        }
        std::cout << " " << keys[i];
    }
    if (!leaf) {
        children[i]->traverse();
    }
}

// Search for a key in the subtree rooted at this node
BTreeNode* BTreeNode::search(int k) {
    int i = 0;
    while (i < keys.size() && k > keys[i]) {
        i++;
    }
    
    if (i < keys.size() && keys[i] == k) {
        return this;
    }
    if (leaf) {
        return nullptr;
    }
    
    return children[i]->search(k);
}

// Insert a new key into a guaranteed non-full node
void BTreeNode::insertNonFull(int k) {
    int i = keys.size() - 1;
    
    if (leaf) {
        keys.push_back(0); // Temporary placeholder to expand the vector
        while (i >= 0 && keys[i] > k) {
            keys[i + 1] = keys[i];
            i--;
        }
        keys[i + 1] = k;
    } else {
        while (i >= 0 && keys[i] > k) {
            i--;
        }
        if (children[i + 1]->keys.size() == 2 * t - 1) {
            splitChild(i + 1, children[i + 1]);
            if (keys[i + 1] < k) {
                i++;
            }
        }
        children[i + 1]->insertNonFull(k);
    }
}

// Split the child 'y' of this node at index 'i'
void BTreeNode::splitChild(int i, BTreeNode* y) {
    BTreeNode* z = new BTreeNode(y->t, y->leaf);
    
    for (int j = 0; j < t - 1; j++) {
        z->keys.push_back(y->keys[j + t]);
    }
    if (!y->leaf) {
        for (int j = 0; j < t; j++) {
            z->children.push_back(y->children[j + t]);
        }
    }
    
    y->keys.resize(t - 1);
    if (!y->leaf) {
        y->children.resize(t);
    }

    children.insert(children.begin() + i + 1, z);
    keys.insert(keys.begin() + i, y->keys[t - 1]);
}

// Wrapper function to remove a key from the subtree
void BTreeNode::remove(int k) {
    int idx = 0;
    while (idx < keys.size() && keys[idx] < k) {
        ++idx;
    }

    if (idx < keys.size() && keys[idx] == k) {
        if (leaf) {
            removeFromLeaf(idx);
        } else {
            removeFromNonLeaf(idx);
        }
    } else {
        if (leaf) {
            return;
        }
        
        bool flag = (idx == keys.size());
        if (children[idx]->keys.size() < t) {
            fill(idx);
        }
        
        if (flag && idx > keys.size()) {
            children[idx - 1]->remove(k);
        } else {
            children[idx]->remove(k);
        }
    }
}

// Delete a key directly from a leaf node
void BTreeNode::removeFromLeaf(int idx) {
    keys.erase(keys.begin() + idx);
}

// Handle key removal from an internal (non-leaf) node
void BTreeNode::removeFromNonLeaf(int idx) {
    int k = keys[idx];
    
    if (children[idx]->keys.size() >= t) {
        int pred = getPred(idx);
        keys[idx] = pred;
        children[idx]->remove(pred);
    } else if (children[idx + 1]->keys.size() >= t) {
        int succ = getSucc(idx);
        keys[idx] = succ;
        children[idx + 1]->remove(succ);
    } else {
        merge(idx);
        children[idx]->remove(k);
    }
}

// Retrieve the predecessor of the key at index 'idx'
int BTreeNode::getPred(int idx) {
    BTreeNode* cur = children[idx];
    while (!cur->leaf) {
        cur = cur->children.back();
    }
    return cur->keys.back();
}

// Retrieve the successor of the key at index 'idx'
int BTreeNode::getSucc(int idx) {
    BTreeNode* cur = children[idx + 1];
    while (!cur->leaf) {
        cur = cur->children.front();
    }
    return cur->keys.front();
}

// Ensure the child at children[idx] has at least 't' keys
void BTreeNode::fill(int idx) {
    if (idx != 0 && children[idx - 1]->keys.size() >= t) {
        borrowFromPrev(idx);
    } else if (idx != keys.size() && children[idx + 1]->keys.size() >= t) {
        borrowFromNext(idx);
    } else {
        if (idx != keys.size()) {
            merge(idx);
        } else {
            merge(idx - 1);
        }
    }
}

// Transfer a key from the previous sibling into children[idx]
void BTreeNode::borrowFromPrev(int idx) {
    BTreeNode* child = children[idx];
    BTreeNode* sibling = children[idx - 1];

    child->keys.insert(child->keys.begin(), keys[idx - 1]);
    if (!child->leaf) {
        child->children.insert(child->children.begin(), sibling->children.back());
    }

    keys[idx - 1] = sibling->keys.back();
    sibling->keys.pop_back();
    if (!sibling->leaf) {
        sibling->children.pop_back();
    }
}

// Transfer a key from the next sibling into children[idx]
void BTreeNode::borrowFromNext(int idx) {
    BTreeNode* child = children[idx];
    BTreeNode* sibling = children[idx + 1];

    child->keys.push_back(keys[idx]);
    if (!child->leaf) {
        child->children.push_back(sibling->children.front());
    }

    keys[idx] = sibling->keys.front();
    sibling->keys.erase(sibling->keys.begin());
    if (!sibling->leaf) {
        sibling->children.erase(sibling->children.begin());
    }
}

// Merge children[idx] with children[idx+1]
void BTreeNode::merge(int idx) {
    BTreeNode* child = children[idx];
    BTreeNode* sibling = children[idx + 1];

    child->keys.push_back(keys[idx]);
    for (size_t i = 0; i < sibling->keys.size(); ++i) {
        child->keys.push_back(sibling->keys[i]);
    }
    
    if (!child->leaf) {
        for (size_t i = 0; i < sibling->children.size(); ++i) {
            child->children.push_back(sibling->children[i]);
        }
    }

    keys.erase(keys.begin() + idx);
    children.erase(children.begin() + idx + 1);
    delete sibling;
}



// BTree Implementation

// Initialize tree with minimum degree configuration
BTree::BTree(int _t) : root(nullptr), t(_t) {}

// Display tree contents from the root
void BTree::traverse() {
    if (root != nullptr) {
        root->traverse();
    }
    std::cout << "\n";
}

// Look up a key from the root node downward
BTreeNode* BTree::search(int k) {
    return (root == nullptr) ? nullptr : root->search(k);
}

// Main insertion interface
void BTree::insert(int k) {
    if (root == nullptr) {
        root = new BTreeNode(t, true);
        root->keys.push_back(k);
    } else {
        if (root->keys.size() == 2 * t - 1) {
            BTreeNode* s = new BTreeNode(t, false);
            s->children.push_back(root);
            s->splitChild(0, root);
            
            int i = 0;
            if (s->keys[0] < k) {
                i++;
            }
            s->children[i]->insertNonFull(k);
            root = s;
        } else {
            root->insertNonFull(k);
        }
    }
}

// Main removal interface
void BTree::remove(int k) {
    if (!root) {
        return;
    }
    
    root->remove(k);
    
    // Adjust root if it becomes empty post-removal
    if (root->keys.size() == 0) {
        BTreeNode* tmp = root;
        if (root->leaf) {
            root = nullptr;
        } else {
            root = root->children[0];
        }
        delete tmp;
    }
}