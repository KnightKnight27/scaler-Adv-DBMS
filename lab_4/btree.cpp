#include <iostream>
#include <vector>
#include <algorithm>

/**
 * B-Tree Implementation (order T = minimum degree)
 * 
 * Properties:
 * - Every internal node has between t-1 and 2t-1 keys
 * - Every internal node has between t and 2t children
 * - Leaf nodes have between t-1 and 2t-1 keys
 * - Root may have as few as 1 key
 * - All leaves at the same level (perfect balance)
 * 
 * Operations:
 * - INSERT: Split full nodes on the way down
 * - DELETE: Merge/borrow on the way down
 * - SEARCH: Navigate from root to leaf
 */

const int T = 2;   // Minimum degree (change to 3, 4, etc. for higher fanout)

struct BNode {
    std::vector<int>    keys;
    std::vector<BNode*> children;
    bool                leaf = true;

    BNode() = default;
};

class BTree {
private:
    BNode* root = nullptr;

    /**
     * split_child: Split full child (2T-1 keys) at index i of parent
     * 
     * Before:
     * parent: [... keys[i-1] | ? | keys[i] ...]
     *                children[i] (FULL: 2T-1 keys)
     * 
     * After:
     * parent: [... keys[i-1] | median | keys[i] ...]
     *                left (T-1)  ↑  right (T-1)
     */
    void split_child(BNode* parent, int i) {
        BNode* y = parent->children[i];  // Full child
        BNode* z = new BNode();           // New right sibling
        z->leaf = y->leaf;

        // Move right half of keys to z
        int median_idx = T - 1;
        int median_key = y->keys[median_idx];
        
        z->keys.assign(y->keys.begin() + T, y->keys.end());
        y->keys.resize(T - 1);

        // Move right half of children to z (if not leaf)
        if (!y->leaf) {
            z->children.assign(y->children.begin() + T, y->children.end());
            y->children.resize(T);
        }

        // Insert median key into parent
        parent->keys.insert(parent->keys.begin() + i, median_key);
        parent->children.insert(parent->children.begin() + i + 1, z);
    }

    /**
     * insert_non_full: Insert key into node that is not full
     */
    void insert_non_full(BNode* node, int key) {
        int i = (int)node->keys.size() - 1;

        if (node->leaf) {
            // Leaf node: insert key in sorted position
            node->keys.push_back(0);
            while (i >= 0 && key < node->keys[i]) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            node->keys[i + 1] = key;
        } else {
            // Internal node: find child to recurse into
            while (i >= 0 && key < node->keys[i])
                i--;
            i++;

            // Split child if full
            if ((int)node->children[i]->keys.size() == 2 * T - 1) {
                split_child(node, i);
                if (key > node->keys[i])
                    i++;
            }
            insert_non_full(node->children[i], key);
        }
    }

    /**
     * get_predecessor: Get largest key in left subtree
     */
    int get_predecessor(BNode* node, int idx) {
        BNode* cur = node->children[idx];
        while (!cur->leaf)
            cur = cur->children.back();
        return cur->keys.back();
    }

    /**
     * get_successor: Get smallest key in right subtree
     */
    int get_successor(BNode* node, int idx) {
        BNode* cur = node->children[idx + 1];
        while (!cur->leaf)
            cur = cur->children.front();
        return cur->keys.front();
    }

    /**
     * merge: Merge children[idx] and children[idx+1] around keys[idx]
     */
    void merge(BNode* node, int idx) {
        BNode* left = node->children[idx];
        BNode* right = node->children[idx + 1];

        // Pull key from parent and merge with right sibling
        left->keys.push_back(node->keys[idx]);
        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());

        if (!left->leaf)
            left->children.insert(left->children.end(),
                                  right->children.begin(), right->children.end());

        // Remove key from parent
        node->keys.erase(node->keys.begin() + idx);
        node->children.erase(node->children.begin() + idx + 1);

        delete right;
    }

    /**
     * fill: Ensure child[idx] has at least T keys before descending
     */
    void fill(BNode* node, int idx) {
        // Borrow from left sibling
        if (idx > 0 && (int)node->children[idx - 1]->keys.size() >= T) {
            BNode* child = node->children[idx];
            BNode* sibling = node->children[idx - 1];

            child->keys.insert(child->keys.begin(), node->keys[idx - 1]);
            node->keys[idx - 1] = sibling->keys.back();
            sibling->keys.pop_back();

            if (!child->leaf) {
                child->children.insert(child->children.begin(), sibling->children.back());
                sibling->children.pop_back();
            }
        }
        // Borrow from right sibling
        else if (idx < (int)node->children.size() - 1 &&
                 (int)node->children[idx + 1]->keys.size() >= T) {
            BNode* child = node->children[idx];
            BNode* sibling = node->children[idx + 1];

            child->keys.push_back(node->keys[idx]);
            node->keys[idx] = sibling->keys.front();
            sibling->keys.erase(sibling->keys.begin());

            if (!child->leaf) {
                child->children.push_back(sibling->children.front());
                sibling->children.erase(sibling->children.begin());
            }
        }
        // Merge with sibling
        else {
            if (idx < (int)node->children.size() - 1)
                merge(node, idx);
            else
                merge(node, idx - 1);
        }
    }

    /**
     * delete_key: Recursively delete key from subtree rooted at node
     */
    void delete_key(BNode* node, int key) {
        int idx = 0;
        while (idx < (int)node->keys.size() && key > node->keys[idx])
            idx++;

        if (idx < (int)node->keys.size() && node->keys[idx] == key) {
            // Key found in this node
            if (node->leaf) {
                // Leaf: simply remove
                node->keys.erase(node->keys.begin() + idx);
            } else if ((int)node->children[idx]->keys.size() >= T) {
                // Replace with predecessor
                int pred = get_predecessor(node, idx);
                node->keys[idx] = pred;
                delete_key(node->children[idx], pred);
            } else if ((int)node->children[idx + 1]->keys.size() >= T) {
                // Replace with successor
                int succ = get_successor(node, idx);
                node->keys[idx] = succ;
                delete_key(node->children[idx + 1], succ);
            } else {
                // Merge and recurse
                merge(node, idx);
                delete_key(node->children[idx], key);
            }
        } else {
            // Key not in this node, recurse to child
            if (node->leaf) {
                return;  // Key not found, silently return
            }

            bool is_last = (idx >= (int)node->children.size());
            int child_idx = is_last ? idx - 1 : idx;
            
            if (child_idx < (int)node->children.size() && 
                (int)node->children[child_idx]->keys.size() < T) {
                fill(node, child_idx);
            }

            if (is_last && idx > (int)node->children.size())
                delete_key(node->children[node->children.size() - 1], key);
            else if (idx < (int)node->children.size())
                delete_key(node->children[idx], key);
        }
    }

    /**
     * search_helper: Recursive search
     */
    bool search_helper(BNode* node, int key) const {
        int i = 0;
        while (i < (int)node->keys.size() && key > node->keys[i])
            i++;

        if (i < (int)node->keys.size() && node->keys[i] == key)
            return true;

        if (node->leaf)
            return false;

        return search_helper(node->children[i], key);
    }

    /**
     * inorder: Inorder traversal for sorted output
     */
    void inorder(BNode* node) const {
        if (!node) return;

        for (int i = 0; i < (int)node->keys.size(); i++) {
            if (!node->leaf)
                inorder(node->children[i]);
            std::cout << node->keys[i] << " ";
        }

        if (!node->leaf)
            inorder(node->children.back());
    }

public:
    /**
     * insert: Insert a key into the B-Tree
     */
    void insert(int key) {
        if (!root) {
            root = new BNode();
            root->keys.push_back(key);
            return;
        }

        // If root is full, split it
        if ((int)root->keys.size() == 2 * T - 1) {
            BNode* s = new BNode();
            s->leaf = false;
            s->children.push_back(root);
            split_child(s, 0);
            root = s;
        }

        insert_non_full(root, key);
    }

    /**
     * remove: Delete a key from the B-Tree
     */
    void remove(int key) {
        if (!root) return;

        delete_key(root, key);

        // If root is empty after deletion, make its only child the new root
        if (root->keys.empty() && !root->leaf) {
            BNode* old_root = root;
            root = root->children[0];
            delete old_root;
        }
    }

    /**
     * search: Check if key exists in the B-Tree
     */
    bool search(int key) const {
        return root && search_helper(root, key);
    }

    /**
     * print: Print tree in inorder (sorted)
     */
    void print() const {
        inorder(root);
        std::cout << "\n";
    }
};


/**
 * Test B-Tree implementation
 */
int main() {
    std::cout << "=== B-Tree Implementation (T=" << T << ") ===" << std::endl;
    std::cout << "On-disk index structure used by PostgreSQL, MySQL, SQLite" << std::endl;
    std::cout << "Node capacity: " << (T-1) << " to " << (2*T-1) << " keys" << std::endl << std::endl;

    BTree bt;

    // Test 1: Insert sequence from lab spec
    std::cout << "Test 1: Insert sequence {10, 20, 5, 6, 12, 30, 7, 17, 3, 1, 25}" << std::endl;
    int keys[] = {10, 20, 5, 6, 12, 30, 7, 17, 3, 1, 25};

    for (int k : keys) {
        std::cout << "Inserting " << k << "..." << std::endl;
        bt.insert(k);
    }

    std::cout << "\nInorder traversal after inserts:" << std::endl;
    bt.print();

    // Test 2: Search
    std::cout << "\nTest 2: Search operations" << std::endl;
    int search_keys[] = {17, 99, 6, 50};
    for (int k : search_keys) {
        std::cout << "Search " << k << ": "
                  << (bt.search(k) ? "FOUND" : "NOT FOUND") << std::endl;
    }

    // Test 3: Delete
    std::cout << "\nTest 3: Delete operations" << std::endl;
    std::cout << "Deleting 6..." << std::endl;
    bt.remove(6);
    std::cout << "Inorder after deleting 6:" << std::endl;
    bt.print();

    std::cout << "\nDeleting 20..." << std::endl;
    bt.remove(20);
    std::cout << "Inorder after deleting 20:" << std::endl;
    bt.print();

    // Test 4: Larger sequence
    std::cout << "\nTest 4: Larger insertion sequence (1-20)" << std::endl;
    BTree bt2;
    for (int i = 1; i <= 20; i++) {
        bt2.insert(i);
    }

    std::cout << "Inorder traversal:" << std::endl;
    bt2.print();

    std::cout << "\nDeleting even numbers..." << std::endl;
    for (int i = 2; i <= 20; i += 2) {
        bt2.remove(i);
    }

    std::cout << "Inorder after deletions:" << std::endl;
    bt2.print();

    std::cout << "\n✓ B-Tree tests complete!" << std::endl;

    return 0;
}
