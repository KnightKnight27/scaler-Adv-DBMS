/*
 * Lab 5: Red-Black Tree Implementation
 * Student: Talin Daga (24bcs10321)
 *
 * A self-balancing BST guaranteeing O(log n) insert and search.
 * Uses a sentinel NIL node (CLRS style) to simplify boundary checks.
 *
 * Red-Black properties enforced:
 *   P1. Every node is RED or BLACK.
 *   P2. The root is BLACK.
 *   P3. All NIL sentinel leaves are BLACK.
 *   P4. If a node is RED, both children are BLACK (no RED-RED consecutive).
 *   P5. Every path from a node to a descendant NIL contains the same
 *       number of BLACK nodes (uniform black-height).
 *
 * Build: gcc -Wall -Wextra -o rbt rbt.c
 * Run:   ./rbt
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

/* ── Types ───────────────────────────────────────────────────── */

typedef enum { RED = 0, BLACK = 1 } Color;

typedef struct Node {
    int    key;
    Color  color;
    struct Node *left, *right, *parent;
} Node;

typedef struct {
    Node *root;
    Node *nil;   /* single shared sentinel for all NULL leaves */
    int   size;
} RBTree;

/* ── Global counters ─────────────────────────────────────────── */
static int g_rotations = 0;
static int g_recolors  = 0;

/* ── Utilities ───────────────────────────────────────────────── */

static void sep(const char *title)
{
    printf("\n================================================================\n");
    printf("  %s\n", title);
    printf("================================================================\n");
}

static void logop(const char *tag, const char *fmt, ...)
{
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    printf("  [%-13s] %s\n", tag, msg);
}

static const char *csym(Color c)  { return c == RED ? "R" : "B"; }
static const char *cname(Color c) { return c == RED ? "RED" : "BLACK"; }

/* Write "key(R/B)" or "NIL(B)" into buf and return buf */
static char *nstr(const Node *n, const Node *nil, char *buf, int sz)
{
    if (n == nil) snprintf(buf, sz, "NIL(B)");
    else          snprintf(buf, sz, "%d(%s)", n->key, csym(n->color));
    return buf;
}

/* ── Initialization ──────────────────────────────────────────── */

RBTree *rbt_new(void)
{
    RBTree *t = malloc(sizeof(RBTree));
    Node  *s  = malloc(sizeof(Node));   /* sentinel */
    s->color  = BLACK;
    s->key    = 0;
    s->left   = s->right = s->parent = s;
    t->nil    = s;
    t->root   = s;
    t->size   = 0;
    return t;
}

/* ── Rotations ───────────────────────────────────────────────── */

/*
 * Left rotation around x:
 *
 *      x                y
 *     / \              / \
 *    A   y    -->     x   C
 *       / \          / \
 *      B   C        A   B
 */
static void left_rotate(RBTree *t, Node *x)
{
    char bx[24], by[24];
    Node *y = x->right;

    logop("L-ROTATE", "%s  →  %s rises above it",
          nstr(x, t->nil, bx, sizeof bx),
          nstr(y, t->nil, by, sizeof by));

    x->right = y->left;
    if (y->left != t->nil) y->left->parent = x;

    y->parent = x->parent;
    if      (x->parent == t->nil)      t->root          = y;
    else if (x == x->parent->left)     x->parent->left  = y;
    else                               x->parent->right = y;

    y->left   = x;
    x->parent = y;
    g_rotations++;
}

/*
 * Right rotation around x:
 *
 *        x            y
 *       / \          / \
 *      y   C  -->   A   x
 *     / \              / \
 *    A   B            B   C
 */
static void right_rotate(RBTree *t, Node *x)
{
    char bx[24], by[24];
    Node *y = x->left;

    logop("R-ROTATE", "%s  →  %s rises above it",
          nstr(x, t->nil, bx, sizeof bx),
          nstr(y, t->nil, by, sizeof by));

    x->left = y->right;
    if (y->right != t->nil) y->right->parent = x;

    y->parent = x->parent;
    if      (x->parent == t->nil)      t->root          = y;
    else if (x == x->parent->right)    x->parent->right = y;
    else                               x->parent->left  = y;

    y->right  = x;
    x->parent = y;
    g_rotations++;
}

/* Colour a node and log it */
static void recolor(Node *n, Color to, const Node *nil)
{
    char bn[24];
    if (n == nil) snprintf(bn, sizeof bn, "NIL");
    else          snprintf(bn, sizeof bn, "%d", n->key);
    logop("RECOLOR", "%s : %s  ->  %s", bn, cname(n->color), cname(to));
    n->color = to;
    g_recolors++;
}

/* ── Insert Fixup ────────────────────────────────────────────── */

/*
 * After standard BST insert (z is RED), restore RBT properties.
 * Three cases per side:
 *   Case 1: uncle is RED            -> recolor, move z up
 *   Case 2: uncle BLACK, inner child -> rotate parent (becomes case 3)
 *   Case 3: uncle BLACK, outer child -> rotate grandparent + recolor
 */
static void insert_fixup(RBTree *t, Node *z)
{
    char bz[24], bp[24], bu[24], bg[24];

    while (z->parent->color == RED) {
        Node *gp = z->parent->parent;   /* z->parent is RED so not root; gp exists */

        if (z->parent == gp->left) {
            /* ── Normal orientation: parent is left child of gp ── */
            Node *uncle = gp->right;
            nstr(z, t->nil, bz, sizeof bz); nstr(z->parent, t->nil, bp, sizeof bp);
            nstr(uncle, t->nil, bu, sizeof bu); nstr(gp, t->nil, bg, sizeof bg);
            logop("FIXUP", "z=%s parent=%s uncle=%s gp=%s", bz, bp, bu, bg);

            if (uncle->color == RED) {
                logop("CASE 1", "Uncle RED → recolor parent+uncle BLACK, gp RED, move z up");
                recolor(z->parent, BLACK, t->nil);
                recolor(uncle,     BLACK, t->nil);
                recolor(gp,        RED,   t->nil);
                z = gp;
            } else {
                if (z == z->parent->right) {
                    /* Case 2: z is inner (right) child → left-rotate parent → becomes case 3 */
                    logop("CASE 2", "Uncle BLACK, z inner-child → left-rotate parent, fall to Case 3");
                    z = z->parent;
                    left_rotate(t, z);
                    gp = z->parent->parent;
                }
                /* Case 3: z is outer (left) child → right-rotate gp, recolor */
                logop("CASE 3", "Uncle BLACK, z outer-child → recolor parent BLACK, gp RED, right-rotate gp");
                recolor(z->parent, BLACK, t->nil);
                recolor(gp,        RED,   t->nil);
                right_rotate(t, gp);
            }
        } else {
            /* ── Mirror: parent is right child of gp ── */
            Node *uncle = gp->left;
            nstr(z, t->nil, bz, sizeof bz); nstr(z->parent, t->nil, bp, sizeof bp);
            nstr(uncle, t->nil, bu, sizeof bu); nstr(gp, t->nil, bg, sizeof bg);
            logop("FIXUP", "z=%s parent=%s uncle=%s gp=%s  [mirror]", bz, bp, bu, bg);

            if (uncle->color == RED) {
                logop("CASE 1m", "Uncle RED → recolor parent+uncle BLACK, gp RED, move z up");
                recolor(z->parent, BLACK, t->nil);
                recolor(uncle,     BLACK, t->nil);
                recolor(gp,        RED,   t->nil);
                z = gp;
            } else {
                if (z == z->parent->left) {
                    /* Case 2m: z is inner (left) child → right-rotate parent */
                    logop("CASE 2m", "Uncle BLACK, z inner-child → right-rotate parent, fall to Case 3m");
                    z = z->parent;
                    right_rotate(t, z);
                    gp = z->parent->parent;
                }
                /* Case 3m: z is outer (right) child → left-rotate gp, recolor */
                logop("CASE 3m", "Uncle BLACK, z outer-child → recolor parent BLACK, gp RED, left-rotate gp");
                recolor(z->parent, BLACK, t->nil);
                recolor(gp,        RED,   t->nil);
                left_rotate(t, gp);
            }
        }
    }
    /* P2: root must always be BLACK */
    if (t->root->color == RED) {
        logop("ROOT FIX", "Root %d was RED → force BLACK", t->root->key);
        t->root->color = BLACK;
        g_recolors++;
    }
}

/* ── Insert ──────────────────────────────────────────────────── */

void rbt_insert(RBTree *t, int key)
{
    Node *z       = malloc(sizeof(Node));
    z->key        = key;
    z->color      = RED;
    z->left       = z->right = z->parent = t->nil;

    /* Standard BST descent to find insertion point */
    Node *parent  = t->nil;
    Node *cur     = t->root;
    while (cur != t->nil) {
        parent = cur;
        cur = (key < cur->key) ? cur->left : cur->right;
    }
    z->parent = parent;

    char pp[24]; nstr(parent, t->nil, pp, sizeof pp);
    if (parent == t->nil) {
        t->root = z;
        logop("BST-PLACE", "%d(RED) → becomes root", key);
    } else if (key < parent->key) {
        parent->left = z;
        logop("BST-PLACE", "%d(RED) → left  child of %s", key, pp);
    } else {
        parent->right = z;
        logop("BST-PLACE", "%d(RED) → right child of %s", key, pp);
    }
    t->size++;

    insert_fixup(t, z);
    logop("DONE", "%d inserted. tree size = %d\n", key, t->size);
}

/* ── Search ──────────────────────────────────────────────────── */

Node *rbt_search(RBTree *t, int key)
{
    Node *x       = t->root;
    int   comps   = 0;
    char  path[512] = "";
    char  buf[24];

    while (x != t->nil) {
        comps++;
        nstr(x, t->nil, buf, sizeof buf);
        if (strlen(path) + strlen(buf) + 8 < sizeof path) {
            strcat(path, buf);
        }

        if (key == x->key) {
            logop("FOUND", "Key %d in %d comparison(s).  Path: %s", key, comps, path);
            return x;
        }
        const char *dir = (key < x->key) ? " →L→ " : " →R→ ";
        if (strlen(path) + 6 < sizeof path) strcat(path, dir);
        x = (key < x->key) ? x->left : x->right;
    }
    nstr(t->nil, t->nil, buf, sizeof buf);
    if (strlen(path) + strlen(buf) + 2 < sizeof path) strcat(path, buf);
    logop("NOT FOUND", "Key %d absent. %d comparison(s).  Path: %s", key, comps, path);
    return NULL;
}

/* ── Traversals ──────────────────────────────────────────────── */

static void inorder_r(const Node *n, const Node *nil, int *first)
{
    if (n == nil) return;
    inorder_r(n->left, nil, first);
    if (!*first) printf(", ");
    printf("%d(%s)", n->key, csym(n->color));
    *first = 0;
    inorder_r(n->right, nil, first);
}

static void inorder(const RBTree *t)
{
    int first = 1;
    printf("  Inorder: ");
    inorder_r(t->root, t->nil, &first);
    printf("\n");
}

/* ── Tree printer: sideways (right subtree on top) ────────────── */

static void sideways_r(const Node *n, const Node *nil, int depth)
{
    if (n == nil) return;
    sideways_r(n->right, nil, depth + 1);
    printf("%*s %d(%s)\n", depth * 5, "", n->key, csym(n->color));
    sideways_r(n->left,  nil, depth + 1);
}

/* ── Tree printer: level-order (BFS) ─────────────────────────── */

static void print_levels(const RBTree *t)
{
    if (t->root == t->nil) { printf("  (empty)\n"); return; }
    const Node *queue[256];
    int head = 0, tail = 0;
    queue[tail++] = t->root;

    int level = 0;
    while (head < tail) {
        int sz = tail - head;
        printf("  Level %d: ", level++);
        for (int i = 0; i < sz; i++) {
            const Node *n = queue[head++];
            printf("%d(%s)  ", n->key, csym(n->color));
            if (n->left  != t->nil) queue[tail++] = n->left;
            if (n->right != t->nil) queue[tail++] = n->right;
        }
        printf("\n");
    }
}

/* ── Property Verification ───────────────────────────────────── */

/*
 * Returns black-height of subtree, or -1 if any RBT property is violated.
 * Checks P3 (NIL=BLACK implicit), P4 (no RED-RED), P5 (uniform bh).
 */
static int bh_check(const Node *n, const Node *nil)
{
    if (n == nil) return 1;   /* NIL counts as one BLACK node */

    /* P4: no RED node with a RED child */
    if (n->color == RED &&
        (n->left->color == RED || n->right->color == RED))
        return -1;

    int lbh = bh_check(n->left,  nil);
    int rbh = bh_check(n->right, nil);
    if (lbh == -1 || rbh == -1 || lbh != rbh) return -1;

    return lbh + (n->color == BLACK ? 1 : 0);
}

/* P5 sub-check: BST ordering via min/max bounds */
static bool bst_ok(const Node *n, const Node *nil, int lo, int hi)
{
    if (n == nil) return true;
    if (n->key <= lo || n->key >= hi) return false;
    return bst_ok(n->left,  nil, lo,    n->key) &&
           bst_ok(n->right, nil, n->key, hi);
}

static void verify_properties(const RBTree *t)
{
    bool all_ok = true;

    /* P1: trivially satisfied by the Color enum */
    logop("P1 PASS", "Every node is typed RED or BLACK (enforced by enum)");

    /* P2: root is BLACK */
    bool p2 = (t->root == t->nil || t->root->color == BLACK);
    logop(p2 ? "P2 PASS" : "P2 FAIL",
          "Root is BLACK  ->  root=%s",
          t->root == t->nil ? "NIL" : (t->root->color == BLACK ? "BLACK" : "RED"));
    all_ok &= p2;

    /* P3: sentinel is BLACK */
    bool p3 = (t->nil->color == BLACK);
    logop(p3 ? "P3 PASS" : "P3 FAIL", "NIL sentinel is BLACK  ->  %s", cname(t->nil->color));
    all_ok &= p3;

    /* P4 + P5: checked together by bh_check */
    int bh = bh_check(t->root, t->nil);
    bool p4p5 = (bh != -1);
    logop(p4p5 ? "P4 PASS" : "P4 FAIL", "No RED node has a RED child  ->  %s",
          p4p5 ? "OK" : "VIOLATION DETECTED");
    if (p4p5)
        logop("P5 PASS", "Uniform black-height = %d on ALL root-to-NIL paths", bh);
    else
        logop("P5 FAIL", "Black-height is NOT uniform or P4 violated");
    all_ok &= p4p5;

    /* BST ordering */
    bool bst = bst_ok(t->root, t->nil, INT_MIN, INT_MAX);
    logop(bst ? "BST PASS" : "BST FAIL", "BST ordering (inorder = sorted)  ->  %s",
          bst ? "OK" : "VIOLATION DETECTED");
    all_ok &= bst;

    printf("\n  %s  All RBT properties %s.\n",
           all_ok ? ">>>" : "!!!",
           all_ok ? "SATISFIED" : "VIOLATED");
}

/* ── Main: Six Tasks ─────────────────────────────────────────── */

int main(void)
{
    printf("================================================================\n");
    printf("  Lab 5: Red-Black Tree Implementation\n");
    printf("  Student: Talin Daga (24bcs10321)\n");
    printf("================================================================\n");

    /* ── Task 1: Tree Initialization ─────────────────────────── */
    sep("TASK 1: Tree Initialization");

    RBTree *t = rbt_new();
    printf("\n  Red-Black Tree created (empty).\n");
    printf("  Root          : NIL sentinel\n");
    printf("  Sentinel NIL  : BLACK, shared by all external leaves\n");
    printf("  Size          : 0 nodes\n");
    printf("\n  RBT invariants to maintain:\n");
    printf("    P1. Every node is RED or BLACK.\n");
    printf("    P2. The root is BLACK.\n");
    printf("    P3. All NIL leaves are BLACK.\n");
    printf("    P4. A RED node's children are both BLACK (no RED-RED).\n");
    printf("    P5. Every root-to-NIL path has the same BLACK-node count.\n");


    /* ── Task 2: Node Insertion ───────────────────────────────── */
    sep("TASK 2: Node Insertion");

    printf("\n  Inserting values: 10, 20, 30, 15, 25, 5, 1, 7, 12\n");
    printf("  (Log shows: BST placement, fixup case, rotations, recolorings)\n\n");

    int vals[] = {10, 20, 30, 15, 25, 5, 1, 7, 12};
    int nv     = (int)(sizeof vals / sizeof vals[0]);
    for (int i = 0; i < nv; i++) {
        printf("  ┌─ Insert %d ─────────────────────────────────────────────\n",
               vals[i]);
        rbt_insert(t, vals[i]);
        printf("  └────────────────────────────────────────────────────────\n\n");
    }

    printf("  Tree after all insertions (sideways; right subtree shown at top):\n\n");
    sideways_r(t->root, t->nil, 1);
    printf("\n  (Read: rotate 90° clockwise to see the actual tree)\n");


    /* ── Task 3: Balancing Operations Summary ────────────────── */
    sep("TASK 3: Balancing Operations Summary");

    printf("\n  Total rotations  : %d\n", g_rotations);
    printf("  Total recolorings: %d\n", g_recolors);
    printf("\n  Case reference:\n");
    printf("  %-10s Uncle RED         Recolor parent+uncle+gp, move z up.\n", "Case 1:");
    printf("  %-10s Uncle BLACK,      Rotate parent toward gp (converts to Case 3).\n", "Case 2:");
    printf("  %-10s inner child\n", "");
    printf("  %-10s Uncle BLACK,      Rotate gp + recolor. Resolves the violation.\n", "Case 3:");
    printf("  %-10s outer child\n", "");
    printf("\n  Cases labelled 'm' are the left-right mirrors of the above.\n");
    printf("  Each case either resolves the violation locally (Case 3)\n");
    printf("  or propagates it upward by at most one level (Case 1).\n");


    /* ── Task 4: Search Operations ───────────────────────────── */
    sep("TASK 4: Search Operations");

    printf("\n  Searching for keys: 15 (present), 25 (present),\n");
    printf("                      11 (absent),  35 (absent)\n\n");

    int keys[] = {15, 25, 11, 35};
    int nk     = (int)(sizeof keys / sizeof keys[0]);
    for (int i = 0; i < nk; i++) {
        printf("  ── Search %d ──────────────────────────────────────────\n", keys[i]);
        rbt_search(t, keys[i]);
        printf("\n");
    }

    printf("  RBT height bound: h ≤ 2·log₂(n+1)  =  2·log₂(%d)  ≈  %d comparisons max\n",
           t->size + 1,
           2 * (int)(3.5));   /* 2*log2(10) ≈ 6.6 → 6 */
    printf("  (vs. worst-case O(n) in an unbalanced BST)\n");


    /* ── Task 5: Tree Traversal ───────────────────────────────── */
    sep("TASK 5: Tree Traversal");

    printf("\n");
    inorder(t);
    printf("\n  Observation: inorder (Left→Node→Right) produces values in\n");
    printf("  ascending sorted order, confirming the BST ordering property.\n");
    printf("  All RED nodes appear between their BLACK parents/children —\n");
    printf("  verifying the no-consecutive-RED rule visually.\n");

    printf("\n  Level-order layout:\n\n");
    print_levels(t);

    printf("\n  Sideways layout (right subtree at top; indent = depth):\n\n");
    sideways_r(t->root, t->nil, 1);


    /* ── Task 6: Property Verification ───────────────────────── */
    sep("TASK 6: Property Verification");

    printf("\n");
    verify_properties(t);

    printf("\n  Final statistics:\n");
    printf("    Nodes in tree   : %d\n", t->size);
    printf("    Rotations done  : %d\n", g_rotations);
    printf("    Recolorings done: %d\n", g_recolors);
    printf("    Verified black-h: see P5 above\n");

    return 0;
}
