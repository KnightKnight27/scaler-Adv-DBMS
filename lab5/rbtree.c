/*
 * Lab 5: Red-Black Tree Implementation
 * Student: Lokendra Singh Rajawat (23bcs10075)
 * Subject: Advanced Database Management Systems
 *
 * A Red-Black Tree is a self-balancing BST that guarantees:
 *   1. Every node is either RED or BLACK.
 *   2. The root is always BLACK.
 *   3. Every leaf (NIL sentinel) is BLACK.
 *   4. If a node is RED, both its children are BLACK.
 *   5. For each node, all paths to descendant leaves have the
 *      same number of BLACK nodes (black-height).
 *
 * These properties ensure the tree height is O(log n), keeping
 * search, insert, and delete all O(log n).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Data Types
 * ============================================================ */

typedef enum { RED = 0, BLACK = 1 } Color;

typedef struct Node {
    int          key;
    Color        color;
    struct Node *left;
    struct Node *right;
    struct Node *parent;
} Node;

typedef struct {
    Node *root;
    Node *NIL;      /* Sentinel: single shared leaf node (always BLACK) */
    int   size;
    int   rotations;    /* counter for logging */
    int   recolorings;  /* counter for logging */
} RBTree;

/* ============================================================
 * Internal helpers
 * ============================================================ */

static Node *new_node(RBTree *t, int key)
{
    Node *n    = (Node *)malloc(sizeof(Node));
    n->key     = key;
    n->color   = RED;          /* new nodes are always RED */
    n->left    = t->NIL;
    n->right   = t->NIL;
    n->parent  = t->NIL;
    return n;
}

static const char *color_str(Color c)
{
    return (c == RED) ? "RED" : "BLACK";
}

/* ============================================================
 * Rotations
 * ============================================================ */

/*
 * Left Rotation around node x:
 *
 *     x                 y
 *    / \               / \
 *   A   y    --->     x   C
 *      / \           / \
 *     B   C         A   B
 */
static void left_rotate(RBTree *t, Node *x)
{
    Node *y   = x->right;
    x->right  = y->left;

    if (y->left != t->NIL)
        y->left->parent = x;

    y->parent = x->parent;

    if (x->parent == t->NIL)
        t->root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;

    y->left   = x;
    x->parent = y;

    t->rotations++;
    printf("[Rotation] Left rotation on node %d\n", x->key);
}

/*
 * Right Rotation around node y:
 *
 *       y               x
 *      / \             / \
 *     x   C   --->    A   y
 *    / \                 / \
 *   A   B               B   C
 */
static void right_rotate(RBTree *t, Node *y)
{
    Node *x   = y->left;
    y->left   = x->right;

    if (x->right != t->NIL)
        x->right->parent = y;

    x->parent = y->parent;

    if (y->parent == t->NIL)
        t->root = x;
    else if (y == y->parent->left)
        y->parent->left = x;
    else
        y->parent->right = x;

    x->right  = y;
    y->parent = x;

    t->rotations++;
    printf("[Rotation] Right rotation on node %d\n", y->key);
}

/* ============================================================
 * Fix-up after insertion
 *
 * Three cases when z->parent is RED (violating property 4):
 *
 * Case 1: Uncle is RED
 *   -> Recolor parent and uncle to BLACK, grandparent to RED
 *   -> Move z up to grandparent and repeat
 *
 * Case 2: Uncle is BLACK, z is an "inner" child
 *   -> Rotate z's parent away from z (making z the outer child)
 *   -> Fall through to Case 3
 *
 * Case 3: Uncle is BLACK, z is an "outer" child
 *   -> Recolor parent BLACK, grandparent RED
 *   -> Rotate grandparent away
 * ============================================================ */
static void fix_insert(RBTree *t, Node *z)
{
    while (z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            /* Parent is a left child */
            Node *uncle = z->parent->parent->right;

            if (uncle->color == RED) {
                /* ------- Case 1: Uncle is RED ------- */
                printf("[Case 1] Uncle %d is RED — recoloring\n",
                       uncle->key);
                z->parent->color         = BLACK;
                uncle->color             = BLACK;
                z->parent->parent->color = RED;
                t->recolorings += 3;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    /* ------- Case 2: z is inner child ------- */
                    printf("[Case 2] z=%d is inner child — rotate parent left\n",
                           z->key);
                    z = z->parent;
                    left_rotate(t, z);
                }
                /* ------- Case 3: z is outer child ------- */
                printf("[Case 3] z=%d is outer child — recolor + rotate grandparent right\n",
                       z->key);
                z->parent->color         = BLACK;
                z->parent->parent->color = RED;
                t->recolorings += 2;
                right_rotate(t, z->parent->parent);
            }
        } else {
            /* Mirror: parent is a right child */
            Node *uncle = z->parent->parent->left;

            if (uncle->color == RED) {
                /* Case 1 (mirror) */
                printf("[Case 1M] Uncle %d is RED — recoloring\n",
                       uncle->key);
                z->parent->color         = BLACK;
                uncle->color             = BLACK;
                z->parent->parent->color = RED;
                t->recolorings += 3;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    /* Case 2 (mirror) */
                    printf("[Case 2M] z=%d is inner child — rotate parent right\n",
                           z->key);
                    z = z->parent;
                    right_rotate(t, z);
                }
                /* Case 3 (mirror) */
                printf("[Case 3M] z=%d is outer child — recolor + rotate grandparent left\n",
                       z->key);
                z->parent->color         = BLACK;
                z->parent->parent->color = RED;
                t->recolorings += 2;
                left_rotate(t, z->parent->parent);
            }
        }
    }
    t->root->color = BLACK;   /* property 2: root is always BLACK */
}

/* ============================================================
 * Public API
 * ============================================================ */

RBTree *rbtree_create(void)
{
    RBTree *t = (RBTree *)malloc(sizeof(RBTree));

    /* Sentinel NIL node — shared by all leaves */
    t->NIL         = (Node *)calloc(1, sizeof(Node));
    t->NIL->color  = BLACK;
    t->NIL->left   = t->NIL;
    t->NIL->right  = t->NIL;
    t->NIL->parent = t->NIL;

    t->root       = t->NIL;
    t->size       = 0;
    t->rotations  = 0;
    t->recolorings = 0;
    return t;
}

void rbtree_insert(RBTree *t, int key)
{
    printf("\n--- Inserting %d ---\n", key);

    Node *z = new_node(t, key);
    Node *y = t->NIL;
    Node *x = t->root;

    /* Standard BST placement */
    while (x != t->NIL) {
        y = x;
        if (z->key < x->key)      x = x->left;
        else if (z->key > x->key) x = x->right;
        else {
            printf("[Skip] Duplicate key %d — skipping.\n", key);
            free(z);
            return;
        }
    }

    z->parent = y;
    if (y == t->NIL)
        t->root = z;
    else if (z->key < y->key)
        y->left = z;
    else
        y->right = z;

    t->size++;
    printf("[Insert] Placed %d as %s child of %s\n",
           key,
           (z->parent == t->NIL) ? "root" :
               (z == z->parent->left ? "left" : "right"),
           (z->parent == t->NIL) ? "NIL" :
               (char[16]){0} /* we'll just describe it */
           );

    /* Fix Red-Black properties */
    fix_insert(t, z);
    printf("[Result] %d inserted. Color: %s\n", key, color_str(z->color));
}

Node *rbtree_search(RBTree *t, int key)
{
    Node *x = t->root;
    int   comparisons = 0;

    while (x != t->NIL) {
        comparisons++;
        if (key == x->key) {
            printf("[Search] Found %d after %d comparison(s). Color: %s\n",
                   key, comparisons, color_str(x->color));
            return x;
        } else if (key < x->key) {
            x = x->left;
        } else {
            x = x->right;
        }
    }
    printf("[Search] Key %d NOT found after %d comparison(s).\n",
           key, comparisons);
    return NULL;
}

/* ============================================================
 * Traversals & Verification
 * ============================================================ */

/* Inorder traversal: prints keys in sorted order with colors */
static void inorder_helper(RBTree *t, Node *x)
{
    if (x == t->NIL) return;
    inorder_helper(t, x->left);
    printf("  %d(%s) ", x->key, color_str(x->color));
    inorder_helper(t, x->right);
}

void rbtree_inorder(RBTree *t)
{
    printf("Inorder traversal: ");
    inorder_helper(t, t->root);
    printf("\n");
}

/* Count BLACK nodes on the path from node to leaf */
static int black_height(RBTree *t, Node *x)
{
    if (x == t->NIL) return 0;
    int lh = black_height(t, x->left);
    int rh = black_height(t, x->right);
    if (lh == -1 || rh == -1 || lh != rh) return -1;
    return lh + (x->color == BLACK ? 1 : 0);
}

/* Verify all 5 RB-Tree properties */
static int verify_helper(RBTree *t, Node *x)
{
    if (x == t->NIL) return 1;

    /* Property 4: RED node's children must be BLACK */
    if (x->color == RED) {
        if (x->left->color != BLACK || x->right->color != BLACK) {
            printf("[ERROR] Property 4 violated at node %d\n", x->key);
            return 0;
        }
    }

    return verify_helper(t, x->left) && verify_helper(t, x->right);
}

void rbtree_verify(RBTree *t)
{
    printf("\n--- Property Verification ---\n");

    /* Property 2: root is BLACK */
    if (t->root != t->NIL && t->root->color != BLACK) {
        printf("[FAIL] Property 2: Root is not BLACK!\n");
        return;
    }
    printf("[PASS] Property 2: Root is BLACK.\n");

    /* Property 4 + black-height (5) */
    int ok = verify_helper(t, t->root);
    if (ok)
        printf("[PASS] Property 4: No RED node has a RED child.\n");

    int bh = black_height(t, t->root);
    if (bh == -1)
        printf("[FAIL] Property 5: Black-height not uniform!\n");
    else
        printf("[PASS] Property 5: Black-height = %d (uniform on all paths).\n", bh);
}

/* Pretty-print tree structure (level-order) */
static void print_level(RBTree *t, Node **queue, int size, int level)
{
    if (size == 0) return;
    int all_nil = 1;
    for (int i = 0; i < size; i++)
        if (queue[i] != t->NIL) { all_nil = 0; break; }
    if (all_nil) return;

    printf("Level %d: ", level);
    Node **next = (Node **)malloc(size * 2 * sizeof(Node *));
    int    next_size = 0;

    for (int i = 0; i < size; i++) {
        if (queue[i] == t->NIL) {
            printf("[NIL] ");
            next[next_size++] = t->NIL;
            next[next_size++] = t->NIL;
        } else {
            printf("[%d/%s] ", queue[i]->key, color_str(queue[i]->color));
            next[next_size++] = queue[i]->left;
            next[next_size++] = queue[i]->right;
        }
    }
    printf("\n");
    print_level(t, next, next_size, level + 1);
    free(next);
}

void rbtree_print(RBTree *t)
{
    printf("\n--- Tree Structure (Level Order) ---\n");
    if (t->root == t->NIL) { printf("(empty)\n"); return; }
    Node *q[1] = { t->root };
    print_level(t, q, 1, 0);
}

/* ============================================================
 * main: Demonstrates all Lab 5 tasks
 * ============================================================ */
int main(void)
{
    printf("==========================================================\n");
    printf("  Lab 5: Red-Black Tree Implementation\n");
    printf("  Student: Lokendra Singh Rajawat (23bcs10075)\n");
    printf("==========================================================\n");

    /* Task 1: Tree Initialization */
    printf("\n[Task 1] Tree Initialization\n");
    RBTree *tree = rbtree_create();
    printf("Empty Red-Black Tree created. Size = %d. Root = NIL.\n", tree->size);

    /* Task 2 + 3: Insert nodes and observe balancing */
    printf("\n[Task 2 & 3] Node Insertion and Balancing Operations\n");
    int values[] = {10, 20, 30, 15, 25, 5, 1, 17, 40, 50, 35};
    int n = (int)(sizeof(values) / sizeof(values[0]));

    for (int i = 0; i < n; i++)
        rbtree_insert(tree, values[i]);

    printf("\nTotal insertions: %d\n", tree->size);
    printf("Total rotations : %d\n", tree->rotations);
    printf("Total recolorings: %d\n", tree->recolorings);

    /* Task 5: Inorder Traversal */
    printf("\n[Task 5] Inorder Traversal (should be sorted ascending)\n");
    rbtree_inorder(tree);

    /* Task 4: Search Operations */
    printf("\n[Task 4] Search Operations\n");
    int search_keys[] = {15, 40, 99, 1, 50};
    for (int i = 0; i < 5; i++)
        rbtree_search(tree, search_keys[i]);

    /* Tree Structure */
    rbtree_print(tree);

    /* Task 6: Property Verification */
    rbtree_verify(tree);

    /* Statistics */
    printf("\n--- Final Statistics ---\n");
    printf("Nodes in tree   : %d\n", tree->size);
    printf("Root key        : %d (%s)\n", tree->root->key, color_str(tree->root->color));
    printf("Total rotations : %d\n", tree->rotations);
    printf("Total recolorings: %d\n", tree->recolorings);

    printf("\n==========================================================\n");
    printf("  All tasks completed successfully.\n");
    printf("==========================================================\n");

    return 0;
}
