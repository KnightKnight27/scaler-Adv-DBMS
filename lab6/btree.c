/*
 * Lab 6: B-Tree Index Implementation
 * Student: Lokendra Singh Rajawat (23bcs10075)
 * Subject: Advanced Database Management Systems
 *
 * B-Tree of minimum degree t:
 *   - Every non-root node has at least t-1 keys.
 *   - Every node has at most 2t-1 keys.
 *   - A node with k keys has exactly k+1 children (if non-leaf).
 *   - All leaf nodes are at the same depth.
 *   - Keys within each node are stored in sorted order.
 *
 * We use the "split-on-the-way-down" (proactive split) strategy
 * so that insert always finds a non-full node to insert into —
 * no need to walk back up the tree.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Configuration
 * ============================================================ */
#define T 3   /* Minimum degree: each node holds [t-1 .. 2t-1] keys */
              /* With T=3: min 2 keys, max 5 keys per node           */

/* ============================================================
 * Data Structures
 * ============================================================ */

typedef struct BTreeNode {
    int  keys[2 * T - 1];          /* max 2t-1 keys */
    char values[2 * T - 1][64];    /* associated string values */
    struct BTreeNode *children[2 * T]; /* max 2t children */
    int  n;                         /* current number of keys */
    int  leaf;                      /* 1 if leaf node, 0 otherwise */
} BTreeNode;

typedef struct {
    BTreeNode *root;
    int        height;
    int        total_nodes;
    int        splits;
} BTree;

/* ============================================================
 * Node allocation
 * ============================================================ */

static BTreeNode *btree_new_node(int is_leaf)
{
    BTreeNode *node = (BTreeNode *)calloc(1, sizeof(BTreeNode));
    node->leaf = is_leaf;
    return node;
}

/* ============================================================
 * B-Tree Initialization
 * ============================================================ */

BTree *btree_create(void)
{
    BTree *tree = (BTree *)calloc(1, sizeof(BTree));
    tree->root  = btree_new_node(1);   /* empty leaf root */
    tree->height = 0;
    tree->total_nodes = 1;
    tree->splits = 0;

    printf("[Init] B-Tree created. Degree t=%d\n", T);
    printf("       Min keys per node : %d\n", T - 1);
    printf("       Max keys per node : %d\n", 2 * T - 1);
    printf("       Max children/node : %d\n", 2 * T);
    return tree;
}

/* ============================================================
 * Search
 * ============================================================ */

typedef struct {
    BTreeNode *node;
    int        index;
    int        accesses;
} SearchResult;

static SearchResult btree_search_node(BTreeNode *x, int key, int depth, int *accesses)
{
    SearchResult r = { NULL, -1, 0 };
    int i = 0;
    (*accesses)++;

    /* Find the first key >= target */
    while (i < x->n && key > x->keys[i])
        i++;

    if (i < x->n && key == x->keys[i]) {
        r.node  = x;
        r.index = i;
        return r;
    }

    if (x->leaf) return r;   /* not found */

    return btree_search_node(x->children[i], key, depth + 1, accesses);
}

void btree_search(BTree *tree, int key)
{
    int          accesses = 0;
    SearchResult r = btree_search_node(tree->root, key, 0, &accesses);
    if (r.node) {
        printf("[Search] Key %d FOUND — value: \"%s\" | Node accesses: %d\n",
               key, r.node->values[r.index], accesses);
    } else {
        printf("[Search] Key %d NOT FOUND | Node accesses: %d\n",
               key, accesses);
    }
}

/* ============================================================
 * Insertion helpers
 * ============================================================ */

/*
 * split_child(parent, i, child)
 *
 * The i-th child of parent is full (has 2t-1 keys).
 * We split it at the median (index t-1), promote the median key
 * to parent, and create a new right sibling node with the upper half.
 *
 * Before:
 *   parent: [...] child_ptr[i] = full_node
 *
 * After:
 *   parent: [..., median_key, ...]
 *            child_ptr[i] = left_half  child_ptr[i+1] = right_half
 */
static void split_child(BTree *tree, BTreeNode *parent, int i, BTreeNode *child)
{
    int median_key = child->keys[T - 1];
    printf("[Split] Node full (%d keys). Promoting median key=%d to parent.\n",
           child->n, median_key);

    BTreeNode *right = btree_new_node(child->leaf);
    tree->total_nodes++;
    tree->splits++;

    /* Move the upper t-1 keys and values to right node */
    right->n = T - 1;
    for (int j = 0; j < T - 1; j++) {
        right->keys[j] = child->keys[j + T];
        strncpy(right->values[j], child->values[j + T], 63);
    }

    /* Move children if not a leaf */
    if (!child->leaf) {
        for (int j = 0; j < T; j++)
            right->children[j] = child->children[j + T];
    }

    /* Truncate child to t-1 keys (left half) */
    child->n = T - 1;

    /* Make room in parent for the promoted median key */
    for (int j = parent->n; j >= i + 1; j--)
        parent->children[j + 1] = parent->children[j];
    parent->children[i + 1] = right;

    for (int j = parent->n - 1; j >= i; j--) {
        parent->keys[j + 1] = parent->keys[j];
        strncpy(parent->values[j + 1], parent->values[j], 63);
    }

    parent->keys[i] = median_key;
    strncpy(parent->values[i], child->values[T - 1], 63);
    parent->n++;

    printf("       Left child: %d keys | Right child: %d keys\n",
           child->n, right->n);
}

/*
 * insert_non_full: Insert key into a node that is guaranteed NOT full.
 * Descends the tree, splitting full children on the way down.
 */
static void insert_non_full(BTree *tree, BTreeNode *x, int key, const char *value)
{
    int i = x->n - 1;

    if (x->leaf) {
        /* Shift keys right to make room, then insert */
        while (i >= 0 && key < x->keys[i]) {
            x->keys[i + 1] = x->keys[i];
            strncpy(x->values[i + 1], x->values[i], 63);
            i--;
        }
        x->keys[i + 1] = key;
        strncpy(x->values[i + 1], value, 63);
        x->n++;
        printf("       Inserted key=%d into leaf node. Node now has %d keys.\n",
               key, x->n);
    } else {
        /* Find child to descend into */
        while (i >= 0 && key < x->keys[i])
            i--;
        i++;

        /* If target child is full, split it first */
        if (x->children[i]->n == 2 * T - 1) {
            split_child(tree, x, i, x->children[i]);
            /* After split, decide which of the two children to descend into */
            if (key > x->keys[i])
                i++;
        }
        insert_non_full(tree, x->children[i], key, value);
    }
}

void btree_insert(BTree *tree, int key, const char *value)
{
    printf("\n--- Inserting key=%d, value=\"%s\" ---\n", key, value);

    BTreeNode *r = tree->root;

    if (r->n == 2 * T - 1) {
        /* Root is full — tree grows in height */
        printf("[Split-Root] Root is full. Creating new root and splitting.\n");
        BTreeNode *s = btree_new_node(0);   /* new root (non-leaf) */
        tree->total_nodes++;
        tree->root      = s;
        s->children[0]  = r;
        s->n            = 0;
        split_child(tree, s, 0, r);
        tree->height++;
        insert_non_full(tree, s, key, value);
    } else {
        insert_non_full(tree, r, key, value);
    }
}

/* ============================================================
 * Tree Printing (Level-Order BFS)
 * ============================================================ */

void btree_print(BTree *tree)
{
    printf("\n--- B-Tree Structure (Level Order) ---\n");
    printf("    Degree t=%d | Height=%d | Nodes=%d | Splits=%d\n\n",
           T, tree->height, tree->total_nodes, tree->splits);

    if (!tree->root) { printf("(empty)\n"); return; }

    /* Simple BFS using a queue of (node, level) pairs */
    BTreeNode *queue[1024];
    int        levels[1024];
    int        front = 0, back = 0;

    queue[back]  = tree->root;
    levels[back] = 0;
    back++;

    int cur_level = -1;

    while (front < back) {
        BTreeNode *node  = queue[front];
        int        level = levels[front];
        front++;

        if (level != cur_level) {
            if (cur_level >= 0) printf("\n");
            printf("Level %d: ", level);
            cur_level = level;
        }

        /* Print this node's keys */
        printf("[");
        for (int i = 0; i < node->n; i++) {
            printf("%d", node->keys[i]);
            if (i < node->n - 1) printf(",");
        }
        printf("] ");

        /* Enqueue children */
        if (!node->leaf) {
            for (int i = 0; i <= node->n; i++) {
                if (node->children[i]) {
                    queue[back]  = node->children[i];
                    levels[back] = level + 1;
                    back++;
                }
            }
        }
    }
    printf("\n");
}

/* ============================================================
 * Property Verification
 * ============================================================ */

static int verify_node(BTreeNode *x, int min_key, int max_key,
                       int depth, int *leaf_depth, int is_root)
{
    /* Check key count bounds */
    if (!is_root && x->n < T - 1) {
        printf("[ERROR] Node has only %d keys (min is %d)\n", x->n, T - 1);
        return 0;
    }
    if (x->n > 2 * T - 1) {
        printf("[ERROR] Node has %d keys (max is %d)\n", x->n, 2 * T - 1);
        return 0;
    }

    /* Check keys are sorted and within bounds */
    for (int i = 0; i < x->n; i++) {
        if (x->keys[i] <= min_key || x->keys[i] >= max_key) {
            printf("[ERROR] Key %d violates BST ordering bounds [%d, %d]\n",
                   x->keys[i], min_key, max_key);
            return 0;
        }
        if (i > 0 && x->keys[i] <= x->keys[i - 1]) {
            printf("[ERROR] Keys not sorted: %d >= %d\n",
                   x->keys[i - 1], x->keys[i]);
            return 0;
        }
    }

    if (x->leaf) {
        if (*leaf_depth == -1)
            *leaf_depth = depth;
        else if (*leaf_depth != depth) {
            printf("[ERROR] Leaf depth mismatch: expected %d, got %d\n",
                   *leaf_depth, depth);
            return 0;
        }
        return 1;
    }

    /* Recurse into children */
    for (int i = 0; i <= x->n; i++) {
        int lo = (i == 0)     ? min_key : x->keys[i - 1];
        int hi = (i == x->n)  ? max_key : x->keys[i];
        if (!verify_node(x->children[i], lo, hi, depth + 1, leaf_depth, 0))
            return 0;
    }
    return 1;
}

void btree_verify(BTree *tree)
{
    printf("\n--- B-Tree Property Verification ---\n");
    int leaf_depth = -1;
    int ok = verify_node(tree->root, -2147483648, 2147483647,
                         0, &leaf_depth, 1);
    if (ok) {
        printf("[PASS] All keys sorted within nodes.\n");
        printf("[PASS] All keys satisfy BST ordering.\n");
        printf("[PASS] All leaf nodes at depth %d (uniform).\n", leaf_depth);
        printf("[PASS] Node key counts within [%d, %d].\n", T - 1, 2 * T - 1);
    } else {
        printf("[FAIL] One or more properties violated!\n");
    }
}

/* ============================================================
 * main: Demonstrates all Lab 6 tasks
 * ============================================================ */
int main(void)
{
    printf("==========================================================\n");
    printf("  Lab 6: B-Tree Index Implementation\n");
    printf("  Student: Lokendra Singh Rajawat (23bcs10075)\n");
    printf("==========================================================\n\n");

    /* Task 1: Initialize */
    printf("[Task 1] B-Tree Initialization\n");
    BTree *tree = btree_create();

    /* Task 2 & 3: Insert records and observe node splitting */
    printf("\n[Task 2 & 3] Record Insertion and Node Splitting\n");

    /* Key-value pairs simulating a student index */
    typedef struct { int key; const char *val; } KV;
    KV records[] = {
        {10, "Alice"},   {20, "Bob"},     {5,  "Charlie"},
        {6,  "Diana"},   {12, "Eve"},     {30, "Frank"},
        {7,  "Grace"},   {17, "Heidi"},   {3,  "Ivan"},
        {1,  "Judy"},    {25, "Karl"},    {15, "Laura"},
        {35, "Mallory"}, {40, "Niaj"},    {45, "Oscar"},
        {50, "Peggy"},   {28, "Quinn"},   {22, "Romeo"},
        {11, "Sybil"},   {32, "Trent"}
    };
    int n = (int)(sizeof(records) / sizeof(records[0]));

    for (int i = 0; i < n; i++)
        btree_insert(tree, records[i].key, records[i].val);

    printf("\nTotal insertions : %d\n", n);
    printf("Total node splits: %d\n", tree->splits);
    printf("Tree height      : %d\n", tree->height);
    printf("Total nodes      : %d\n", tree->total_nodes);

    /* Task 5: Tree Structure Analysis */
    printf("\n[Task 5] Tree Structure Analysis\n");
    btree_print(tree);

    /* Task 4: Search Operations */
    printf("\n[Task 4] Search Operations\n");
    int search_keys[] = {15, 40, 99, 7, 50, 28, 100};
    for (int i = 0; i < 7; i++)
        btree_search(tree, search_keys[i]);

    /* Task 6: Indexing behavior summary */
    printf("\n[Task 6] Indexing Behavior Analysis\n");
    printf("With t=%d, each internal node holds %d–%d keys and %d–%d children.\n",
           T, T-1, 2*T-1, T, 2*T);
    printf("This reduces tree height vs a binary tree:\n");
    printf("  Binary tree for %d keys: up to %d levels\n", n, n);
    printf("  B-Tree    for %d keys: %d levels (height=%d)\n",
           n, tree->height + 1, tree->height);
    printf("Each level halves the search space by a factor of ~%d.\n", 2*T);

    /* Property Verification */
    btree_verify(tree);

    printf("\n==========================================================\n");
    printf("  All tasks completed successfully.\n");
    printf("==========================================================\n");
    return 0;
}
