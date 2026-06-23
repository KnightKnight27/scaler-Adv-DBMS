# Lab Session 4: Red-Black Trees & B-Trees

## 🎯 The Goal

In this lab, we will build two of the most important self-balancing tree structures in computer science:

1. **Red-Black Tree (RBT):** An ultra-fast, in-memory binary search tree that keeps itself balanced using a clever coloring system.
2. **B-Tree:** A wide, short tree designed specifically for secondary storage (like SSDs/HDDs). This is the exact data structure behind popular databases like **PostgreSQL, MySQL, and SQLite**.



## Part 1: Red-Black Trees (In-Memory Speed)

### Why do we need it?

A regular Binary Search Tree (BST) can become unbalanced if you insert sorted data (e.g., 1, 2, 3, 4, 5 turns into a straight line). When that happens, your fast $O(\log n)$ lookup speed degrades into a slow $O(n)$ linear scan.

A Red-Black Tree prevents this by enforcing **4 strict rules**:

1. Every node is painted either **Red** or **Black**.
2. The **Root** of the tree is always **Black**.
3. **No Red-to-Red connections:** A Red node cannot have a Red child or a Red parent.
4. **Black-Height Balance:** Every path from a node to its empty leaf pointers must contain the exact same number of Black nodes.

### The Code (`rbt.cpp`)

```cpp
#include <iostream>
#include <initializer_list>

enum Color { RED, BLACK };

struct RBNode {
    int key;
    Color color;
    RBNode *left, *right, *parent;

    explicit RBNode(int k)
        : key(k), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
private:
    RBNode* root = nullptr;

    // Rotations: The physical movements used to rebalance the tree
    void left_rotate(RBNode* x) {
        RBNode* y = x->right;
        x->right = y->left;
        if (y->left) y->left->parent = x;
        y->parent = x->parent;
        if (!x->parent)                root = y;
        else if (x == x->parent->left) x->parent->left  = y;
        else                           x->parent->right = y;
        y->left = x;
        x->parent = y;
    }

    void right_rotate(RBNode* x) {
        RBNode* y = x->left;
        x->left = y->right;
        if (y->right) y->right->parent = x;
        y->parent = x->parent;
        if (!x->parent)                 root = y;
        else if (x == x->parent->right) x->parent->right = y;
        else                            x->parent->left  = y;
        y->right = x;
        x->parent = y;
    }

    // Fixup Functions: Fixing violations of the 4 rules 
    void fix_insert(RBNode* z) {
        while (z->parent && z->parent->color == RED) {
            RBNode* gp = z->parent->parent; // Grandparent
            
            if (z->parent == gp->left) {
                RBNode* uncle = gp->right;
                
                // Case 1: Uncle is Red -> Just recolor
                if (uncle && uncle->color == RED) {
                    z->parent->color = BLACK;
                    uncle->color = BLACK;
                    gp->color = RED;
                    z = gp; // Move up to check grandparent
                } else {
                    // Case 2: Uncle is Black & z forms a triangle -> Rotate parent
                    if (z == z->parent->right) {
                        z = z->parent;
                        left_rotate(z);
                    }
                    // Case 3: Uncle is Black & z forms a straight line -> Rotate grandparent
                    z->parent->color = BLACK;
                    gp->color = RED;
                    right_rotate(gp);
                }
            } else { // Symmetrical Mirror Cases
                RBNode* uncle = gp->left;
                if (uncle && uncle->color == RED) {
                    z->parent->color = BLACK;
                    uncle->color = BLACK;
                    gp->color = RED;
                    z = gp;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        right_rotate(z);
                    }
                    z->parent->color = BLACK;
                    gp->color = RED;
                    left_rotate(gp);
                }
            }
        }
        root->color = BLACK; // Rule 2 insurance
    }

    void transplant(RBNode* u, RBNode* v) {
        if (!u->parent)                root = v;
        else if (u == u->parent->left) u->parent->left  = v;
        else                           u->parent->right = v;
        if (v) v->parent = u->parent;
    }

    RBNode* minimum(RBNode* node) {
        while (node->left) node = node->left;
        return node;
    }

    void fix_delete(RBNode* x, RBNode* x_parent) {
        while (x != root && (!x || x->color == BLACK)) {
            if (x == (x_parent ? x_parent->left : nullptr)) {
                RBNode* w = x_parent->right; // Sibling
                if (w && w->color == RED) {
                    w->color = BLACK;
                    x_parent->color = RED;
                    left_rotate(x_parent);
                    w = x_parent->right;
                }
                if ((!w->left || w->left->color == BLACK) &&
                    (!w->right || w->right->color == BLACK)) {
                    if (w) w->color = RED;
                    x = x_parent; 
                    x_parent = x->parent;
                } else {
                    if (!w->right || w->right->color == BLACK) {
                        if (w->left) w->left->color = BLACK;
                        w->color = RED;
                        right_rotate(w);
                        w = x_parent->right;
                    }
                    w->color = x_parent->color;
                    x_parent->color = BLACK;
                    if (w->right) w->right->color = BLACK;
                    left_rotate(x_parent);
                    x = root;
                }
            } else { // Symmetrical cases for deletion
                RBNode* w = x_parent->left;
                if (w && w->color == RED) {
                    w->color = BLACK;
                    x_parent->color = RED;
                    right_rotate(x_parent);
                    w = x_parent->left;
                }
                if ((!w->right || w->right->color == BLACK) &&
                    (!w->left || w->left->color == BLACK)) {
                    if (w) w->color = RED;
                    x = x_parent; 
                    x_parent = x->parent;
                } else {
                    if (!w->left || w->left->color == BLACK) {
                        if (w->right) w->right->color = BLACK;
                        w->color = RED;
                        left_rotate(w);
                        w = x_parent->left;
                    }
                    w->color = x_parent->color;
                    x_parent->color = BLACK;
                    if (w->left) w->left->color = BLACK;
                    right_rotate(x_parent);
                    x = root;
                }
            }
        }
        if (x) x->color = BLACK;
    }

    void inorder(RBNode* node) const {
        if (!node) return;
        inorder(node->left);
        std::cout << node->key << (node->color == RED ? "R" : "B") << " ";
        inorder(node->right);
    }

public:
    void insert(int key) {
        RBNode* z = new RBNode(key);
        RBNode* y = nullptr;
        RBNode* x = root;
        while (x) {
            y = x;
            x = (z->key < x->key) ? x->left : x->right;
        }
        z->parent = y;
        if (!y)                  root = z;
        else if (z->key < y->key) y->left  = z;
        else                      y->right = z;
        fix_insert(z);
    }

    void remove(int key) {
        RBNode* z = root;
        while (z && z->key != key) {
            z = (key < z->key) ? z->left : z->right;
        }
        if (!z) return;

        RBNode* y = z;
        RBNode* x = nullptr;
        RBNode* x_parent = nullptr;
        Color y_orig_color = y->color;

        if (!z->left) {
            x = z->right; x_parent = z->parent;
            transplant(z, z->right);
        } else if (!z->right) {
            x = z->left; x_parent = z->parent;
            transplant(z, z->left);
        } else {
            y = minimum(z->right);
            y_orig_color = y->color;
            x = y->right;
            if (y->parent == z) { 
                x_parent = y; 
            } else {
                x_parent = y->parent;
                transplant(y, y->right);
                y->right = z->right;
                y->right->parent = y;
            }
            transplant(z, y);
            y->left = z->left; y->left->parent = y;
            y->color = z->color;
        }
        delete z;
        if (y_orig_color == BLACK) fix_delete(x, x_parent);
    }

    void print() const { 
        inorder(root); 
        std::cout << "\n"; 
    }
};

int main() {
    RedBlackTree rbt;
    for (int k : {10, 20, 30, 15, 25, 5, 1}) rbt.insert(k);

    std::cout << "Inorder Traversal (Key + Color R/B):\n";
    rbt.print();

    rbt.remove(20);
    std::cout << "After removing 20:\n";
    rbt.print();
    return 0;
}

```



## 🗄️ Part 2: Full B-Tree (Disk Optimization)

### Why do we need it?

Reading data from a hard drive or SSD is **thousands of times slower** than reading from RAM.

* Binary trees are bad for disks because every step down a path requires chasing a new pointer (which triggers a slow disk read).
* **The Solution:** Make the nodes *wide* and *fat*. If a single node can store hundreds of keys, we can load a massive block of keys into memory with just a single disk read.

### The Rules of Degree $t$:

Think of $t$ as the factor controlling a node's capacity:

* **Minimum keys:** Every node (except the root) must have at least $t - 1$ keys.
* **Maximum keys:** A node can hold at most $2t - 1$ keys. If it hits this limit, it is **full** and must split down the middle!

### The Code (`btree.cpp`)

```cpp
#include <iostream>
#include <vector>
#include <initializer_list>

const int T = 2; // Minimum degree factor. Change this to scale up node capacity.

struct BNode {
    std::vector<int> keys;
    std::vector<BNode*> children;
    bool leaf = true;

    BNode() = default;
};

class BTree {
private:
    BNode* root = nullptr;

    // Splits a full child node down the middle, promoting the median key up
    void split_child(BNode* parent, int i) {
        BNode* y = parent->children[i];
        BNode* z = new BNode(); // New right-side sibling
        z->leaf = y->leaf;

        // Give the right half of y's keys to z
        z->keys.assign(y->keys.begin() + T, y->keys.end());
        int med = y->keys[T - 1]; // Save the middle element
        y->keys.resize(T - 1);

        // If not a leaf, hand over the matching child pointers too
        if (!y->leaf) {
            z->children.assign(y->children.begin() + T, y->children.end());
            y->children.resize(T);
        }

        // Insert the middle key and new pointer into the parent node
        parent->keys.insert(parent->keys.begin() + i, med);
        parent->children.insert(parent->children.begin() + i + 1, z);
    }

    void insert_non_full(BNode* node, int key) {
        int i = (int)node->keys.size() - 1;
        if (node->leaf) {
            // Find where to slide the key inside the leaf array
            node->keys.push_back(0);
            while (i >= 0 && key < node->keys[i]) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            node->keys[i + 1] = key;
        } else {
            // Figure out which child path to descend
            while (i >= 0 && key < node->keys[i]) i--;
            i++;
            
            // Proactive split: if a child is full, split it *before* going down
            if ((int)node->children[i]->keys.size() == 2 * T - 1) {
                split_child(node, i);
                if (key > node->keys[i]) i++;
            }
            insert_non_full(node->children[i], key);
        }
    }

    int get_predecessor(BNode* node, int idx) {
        BNode* cur = node->children[idx];
        while (!cur->leaf) cur = cur->children.back();
        return cur->keys.back();
    }

    int get_successor(BNode* node, int idx) {
        BNode* cur = node->children[idx + 1];
        while (!cur->leaf) cur = cur->children.front();
        return cur->keys.front();
    }

    // Merges child[idx] and child[idx+1] into a single node
    void merge(BNode* node, int idx) {
        BNode* left = node->children[idx];
        BNode* right = node->children[idx + 1];

        left->keys.push_back(node->keys[idx]);
        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
        
        if (!left->leaf) {
            left->children.insert(left->children.end(), right->children.begin(), right->children.end());
        }

        node->keys.erase(node->keys.begin() + idx);
        node->children.erase(node->children.begin() + idx + 1);
        delete right;
    }

    // Ensures child[idx] has enough keys (>= T) before we step into it
    void fill(BNode* node, int idx) {
        if (idx > 0 && (int)node->children[idx - 1]->keys.size() >= T) {
            // Borrow from left neighbor
            BNode* child = node->children[idx];
            BNode* sibling = node->children[idx - 1];
            child->keys.insert(child->keys.begin(), node->keys[idx - 1]);
            node->keys[idx - 1] = sibling->keys.back();
            sibling->keys.pop_back();
            if (!child->leaf) {
                child->children.insert(child->children.begin(), sibling->children.back());
                sibling->children.pop_back();
            }
        } else if (idx < (int)node->children.size() - 1 && (int)node->children[idx + 1]->keys.size() >= T) {
            // Borrow from right neighbor
            BNode* child = node->children[idx];
            BNode* sibling = node->children[idx + 1];
            child->keys.push_back(node->keys[idx]);
            node->keys[idx] = sibling->keys.front();
            sibling->keys.erase(sibling->keys.begin());
            if (!child->leaf) {
                child->children.push_back(sibling->children.front());
                sibling->children.erase(sibling->children.begin());
            }
        } else {
            // If neighbors are poor too, merge them together
            if (idx < (int)node->children.size() - 1) merge(node, idx);
            else                                     merge(node, idx - 1);
        }
    }

    void delete_key(BNode* node, int key) {
        int idx = 0;
        while (idx < (int)node->keys.size() && key > node->keys[idx]) idx++;

        if (idx < (int)node->keys.size() && node->keys[idx] == key) {
            if (node->leaf) {
                node->keys.erase(node->keys.begin() + idx);
            } else if ((int)node->children[idx]->keys.size() >= T) {
                int pred = get_predecessor(node, idx);
                node->keys[idx] = pred;
                delete_key(node->children[idx], pred);
            } else if ((int)node->children[idx + 1]->keys.size() >= T) {
                int succ = get_successor(node, idx);
                node->keys[idx] = succ;
                delete_key(node->children[idx + 1], succ);
            } else {
                merge(node, idx);
                delete_key(node->children[idx], key);
            }
        } else {
            if (node->leaf) {
                std::cout << "Key not found\n";
                return;
            }
            bool last = (idx == (int)node->children.size());
            if ((int)node->children[last ? idx - 1 : idx]->keys.size() < T) {
                fill(node, last ? idx - 1 : idx);
            }
            if (last && idx > (int)node->keys.size()) {
                delete_key(node->children[idx - 1], key);
            } else {
                delete_key(node->children[idx], key);
            }
        }
    }

    void inorder(BNode* node) const {
        if (!node) return;
        for (size_t i = 0; i < node->keys.size(); i++) {
            if (!node->leaf) inorder(node->children[i]);
            std::cout << node->keys[i] << " ";
        }
        if (!node->leaf) inorder(node->children.back());
    }

public:
    void insert(int key) {
        if (!root) {
            root = new BNode();
            root->keys.push_back(key);
            return;
        }
        // If root is full, split it and make the tree taller
        if ((int)root->keys.size() == 2 * T - 1) {
            BNode* s = new BNode();
            s->leaf = false;
            s->children.push_back(root);
            split_child(s, 0);
            root = s;
        }
        insert_non_full(root, key);
    }

    void remove(int key) {
        if (!root) return;
        delete_key(root, key);
        if (root->keys.empty() && !root->leaf) {
            BNode* old = root;
            root = root->children[0];
            delete old;
        }
    }

    bool search(BNode* node, int key) const {
        int i = 0;
        while (i < (int)node->keys.size() && key > node->keys[i]) i++;
        if (i < (int)node->keys.size() && node->keys[i] == key)   return true;
        if (node->leaf)                                           return false;
        return search(node->children[i], key);
    }

    bool search(int key) const { return root && search(root, key); }
    void print() const { inorder(root); std::cout << "\n"; }
};

int main() {
    BTree bt;
    for (int k : {10, 20, 5, 6, 12, 30, 7, 17, 3, 1, 25}) bt.insert(k);

    std::cout << "Inorder Traversal after insertions:\n";
    bt.print();

    std::cout << "Search 17: " << (bt.search(17) ? "found" : "not found") << "\n";
    std::cout << "Search 99: " << (bt.search(99) ? "found" : "not found") << "\n";

    bt.remove(6);
    bt.remove(20);
    std::cout << "Inorder Traversal after removing 6 and 20:\n";
    bt.print();
    return 0;
}

```

---

## Red-Black Tree vs B-Tree Cheat Sheet

| Feature | Red-Black Tree | B-Tree (Order $t$) |
| --- | --- | --- |
| **Where does it live?** | RAM (Fast, volatile memory) | Disk (SSDs, hard drives) |
| **Keys per node** | Exactly 1 key | Ranges from $t-1$ up to $2t-1$ keys |
| **Height profile** | Taller ($O(\log_2 n)$) | Short & flat ($O(\log_t n)$) |
| **Real-world use** | C++ `std::map`, Linux kernel process scheduler | PostgreSQL indexes, OS Filesystems |
| **Hardware trick** | Fast pointer traversal | Grabs a huge continuous array block at once |

> 💡 **Storage Alignment Note** PostgreSQL uses an 8 KB page block layout. A B-Tree allows one single disk fetch to load an entire node block packed with entries, maximizing input/output efficiency.



## 📝 Key Takeaways for Your Notes

* **Red-Black Trees** keep operations fast in memory by recoloring nodes and twisting them via rotations.
* **B-Trees** prioritize minimizing slow hardware disk steps. They expand horizontally, clustering multiple search paths into compact arrays.
* **Underflow Management:** When a B-Tree node drops below the minimum safe occupancy limit ($t-1$), it handles the issue using a structural process: **Borrow** elements from an immediate neighbor if possible; if both neighbors are empty, **Merge** them into a unified storage block.