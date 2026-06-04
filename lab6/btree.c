/*
 * Lab 6: B-Tree Index Implementation
 * Student: Talin Daga (24bcs10321)
 *
 * Minimum degree t = 3:
 *   Max keys / node    = 2t-1 = 5
 *   Min keys (non-root)= t-1  = 2
 *   Max children       = 2t   = 6
 *
 * Insertion uses the CLRS proactive-split strategy: full child nodes
 * are split before recursing into them, so we never need to travel
 * back up the tree.
 *
 * Build: gcc -Wall -Wextra -o btree btree.c
 * Run:   ./btree
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#define T        3
#define MAX_KEYS (2*T - 1)   /* 5 */
#define MAX_CH   (2*T)       /* 6 */
#define VAL_LEN  24

/* ── Data structures ─────────────────────────────────────────── */

typedef struct Node {
    int    keys[MAX_KEYS];
    char   vals[MAX_KEYS][VAL_LEN];
    struct Node *ch[MAX_CH];
    int    n;       /* number of keys currently stored */
    bool   leaf;
} Node;

typedef struct {
    Node *root;
    int   n_keys;    /* total keys across all nodes  */
    int   n_nodes;   /* total nodes allocated        */
    int   n_splits;  /* total split operations done  */
} BTree;

/* ── Utilities ───────────────────────────────────────────────── */

static void sep(const char *title)
{
    printf("\n================================================================\n");
    printf("  %s\n", title);
    printf("================================================================\n");
}

static void logop(const char *tag, const char *fmt, ...)
{
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("  [%-13s] %s\n", tag, buf);
}

/* Format node keys as "[k1, k2, ...]" into caller-supplied buf */
static char *nks(const Node *n, char *buf, int sz)
{
    int p = 0;
    p += snprintf(buf+p, sz-p, "[");
    for (int i = 0; i < n->n; i++) {
        if (i) p += snprintf(buf+p, sz-p, ", ");
        p += snprintf(buf+p, sz-p, "%d", n->keys[i]);
    }
    snprintf(buf+p, sz-p, "]");
    return buf;
}

/* ── Node allocation ─────────────────────────────────────────── */

static Node *new_node(bool leaf)
{
    Node *n = calloc(1, sizeof(Node));   /* calloc zeros ch[] pointers */
    n->leaf = leaf;
    return n;
}

/* ── Split child ─────────────────────────────────────────────── */

/*
 * Split parent->ch[ci] which must be full (n == MAX_KEYS).
 *
 *  Before:  parent  →  ch[ci] = [k0 k1 | k2 | k3 k4]
 *                                ← lhs →  ↑   ← rhs →
 *                                       median
 *
 *  After:   parent gets median key at index ci.
 *           ch[ci]   = lhs [k0, k1]   (T-1 keys)
 *           ch[ci+1] = rhs [k3, k4]   (T-1 keys, new node)
 */
static void split_child(BTree *bt, Node *par, int ci)
{
    Node *full = par->ch[ci];
    char  bf[64], bpar[64], blhs[64], brhs[64], bpar2[64];

    /* Capture state before we modify full->n */
    nks(full, bf,   sizeof bf);
    nks(par,  bpar, sizeof bpar);

    int med_key = full->keys[T - 1];     /* median = keys[2] for t=3 */

    /* --- Build right-hand sibling from keys[T..2T-2] --- */
    Node *rhs = new_node(full->leaf);
    rhs->n = T - 1;
    for (int j = 0; j < T-1; j++) {
        rhs->keys[j] = full->keys[j + T];
        memcpy(rhs->vals[j], full->vals[j + T], VAL_LEN);
    }
    if (!full->leaf)
        for (int j = 0; j < T; j++)
            rhs->ch[j] = full->ch[j + T];

    /* Left half keeps keys[0..T-2] */
    full->n = T - 1;

    /* --- Shift parent's children right to make room for rhs --- */
    for (int j = par->n; j >= ci+1; j--)
        par->ch[j+1] = par->ch[j];
    par->ch[ci+1] = rhs;

    /* --- Shift parent's keys right, insert median --- */
    for (int j = par->n-1; j >= ci; j--) {
        par->keys[j+1] = par->keys[j];
        memcpy(par->vals[j+1], par->vals[j], VAL_LEN);
    }
    par->keys[ci] = med_key;
    snprintf(par->vals[ci], VAL_LEN, "rec_%d", med_key);
    par->n++;

    bt->n_splits++;
    bt->n_nodes++;

    logop("SPLIT", "#%d  node %s (full) at child[%d] of parent %s",
          bt->n_splits, bf, ci, bpar);
    logop("PROMOTE", "median key=%d  →  parent becomes %s",
          med_key, nks(par, bpar2, sizeof bpar2));
    logop("LEFT HALF", "%s  (stays in original node)",
          nks(full, blhs, sizeof blhs));
    logop("RIGHT HALF", "%s  (new right sibling)",
          nks(rhs,  brhs, sizeof brhs));
}

/* ── Insert into non-full node (recursive) ───────────────────── */

static void insert_nonfull(BTree *bt, Node *x, int key, const char *val)
{
    int  i = x->n - 1;
    char bx[64];

    if (x->leaf) {
        /* Shift keys right to find the correct sorted position */
        while (i >= 0 && key < x->keys[i]) {
            x->keys[i+1] = x->keys[i];
            memcpy(x->vals[i+1], x->vals[i], VAL_LEN);
            i--;
        }
        x->keys[i+1] = key;
        snprintf(x->vals[i+1], VAL_LEN, "%.23s", val);
        x->n++;
        bt->n_keys++;
        logop("LEAF INSERT", "Key %d  →  node %s", key, nks(x, bx, sizeof bx));
    } else {
        /* Find the right child to descend into */
        while (i >= 0 && key < x->keys[i]) i--;
        i++;

        if (x->ch[i]->n == MAX_KEYS) {
            /* Proactive split: child is full before we descend */
            logop("PRE-SPLIT", "Child[%d] is full before inserting %d", i, key);
            split_child(bt, x, i);
            /* After split, the median moved up; choose correct side */
            if (key > x->keys[i]) i++;
        }
        insert_nonfull(bt, x->ch[i], key, val);
    }
}

/* ── Public insert ───────────────────────────────────────────── */

void btree_insert(BTree *bt, int key, const char *val)
{
    Node *r = bt->root;
    if (r->n == MAX_KEYS) {
        /* Root is full: create a new root and split the old root */
        Node *s = new_node(false);
        s->ch[0] = r;
        bt->root = s;
        bt->n_nodes++;
        char br[64];
        logop("ROOT FULL", "Root %s is full → new empty root, splitting old root",
              nks(r, br, sizeof br));
        split_child(bt, s, 0);
        insert_nonfull(bt, s, key, val);
    } else {
        insert_nonfull(bt, r, key, val);
    }
    logop("DONE", "Key %d inserted  (total keys=%d)\n", key, bt->n_keys);
}

/* ── Search ──────────────────────────────────────────────────── */

static bool search_r(const Node *x, int key, int depth, int *acc)
{
    (*acc)++;
    int  i = 0;
    char sb[64]; int p = 0;
    p += snprintf(sb+p, sizeof sb-p, "[");
    for (int k = 0; k < x->n; k++) {
        if (k) p += snprintf(sb+p, sizeof sb-p, ",");
        p += snprintf(sb+p, sizeof sb-p, "%d", x->keys[k]);
    }
    snprintf(sb+p, sizeof sb-p, "]");

    while (i < x->n && key > x->keys[i]) i++;

    if (i < x->n && key == x->keys[i]) {
        logop("ACCESS", "Level %d: %s  →  key %d at index %d  val=\"%s\"  FOUND",
              depth, sb, key, i, x->vals[i]);
        return true;
    }
    if (x->leaf) {
        logop("ACCESS", "Level %d: %s  →  key %d not here (leaf)  NOT FOUND",
              depth, sb, key);
        return false;
    }
    logop("ACCESS", "Level %d: %s  →  key %d: descend child[%d]", depth, sb, key, i);
    return search_r(x->ch[i], key, depth+1, acc);
}

void btree_search(const BTree *bt, int key)
{
    int  acc   = 0;
    bool found = search_r(bt->root, key, 0, &acc);
    logop(found ? "FOUND" : "NOT FOUND",
          "Key %d — %d node access(es)\n",
          key, acc);
}

/* ── Printers ────────────────────────────────────────────────── */

/* Depth-first indented tree print */
static void print_indent(const Node *x, int depth)
{
    printf("%*s[", depth * 4, "");
    for (int i = 0; i < x->n; i++) {
        if (i) printf(", ");
        printf("%d", x->keys[i]);
    }
    printf("]%s\n", x->leaf ? "  *leaf*" : "");
    if (!x->leaf)
        for (int i = 0; i <= x->n; i++)
            print_indent(x->ch[i], depth + 1);
}

/* Level-order (BFS) print */
static void print_levels(const BTree *bt)
{
    typedef struct { const Node *nd; int lv; } QE;
    QE q[512];
    int head = 0, tail = 0, cur = 0;

    q[tail++] = (QE){bt->root, 0};
    printf("  Level 0: ");

    while (head < tail) {
        QE e = q[head++];
        if (e.lv > cur) {
            printf("\n  Level %d: ", e.lv);
            cur = e.lv;
        }
        printf("[");
        for (int i = 0; i < e.nd->n; i++) {
            if (i) printf(", ");
            printf("%d", e.nd->keys[i]);
        }
        printf("]  ");
        if (!e.nd->leaf)
            for (int i = 0; i <= e.nd->n; i++)
                q[tail++] = (QE){e.nd->ch[i], e.lv + 1};
    }
    printf("\n");
}

/* ── Statistics ──────────────────────────────────────────────── */

typedef struct { int nodes, keys, height; } Stats;

static Stats get_stats(const Node *x, int depth)
{
    Stats s = {1, x->n, depth};
    if (!x->leaf)
        for (int i = 0; i <= x->n; i++) {
            Stats c = get_stats(x->ch[i], depth + 1);
            s.nodes += c.nodes;
            s.keys  += c.keys;
            if (c.height > s.height) s.height = c.height;
        }
    return s;
}

static void print_stats(const BTree *bt)
{
    Stats s = get_stats(bt->root, 0);
    printf("\n  %-30s %d\n", "Total nodes:",             s.nodes);
    printf("  %-30s %d\n",   "Total keys:",              s.keys);
    printf("  %-30s %d  (root=0)\n", "Tree height:",     s.height);
    printf("  %-30s %d\n",   "Total splits performed:",  bt->n_splits);
    printf("  %-30s %d / %d\n", "Max/min keys per node:", MAX_KEYS, (int)(T-1));
    printf("  %-30s %.1f\n", "Avg keys per node:",       (double)s.keys/s.nodes);
}

/* ── Main: Six Tasks ─────────────────────────────────────────── */

int main(void)
{
    printf("================================================================\n");
    printf("  Lab 6: B-Tree Index Implementation\n");
    printf("  Student: Talin Daga (24bcs10321)\n");
    printf("================================================================\n");

    /* ── Task 1: B-Tree Initialization ───────────────────────── */
    sep("TASK 1: B-Tree Initialization");

    BTree bt = {NULL, 0, 0, 0};
    bt.root    = new_node(true);   /* empty root is a leaf */
    bt.n_nodes = 1;

    printf("\n  B-Tree created with minimum degree t = %d\n\n", T);
    printf("  %-40s %d\n", "Minimum degree (t):",           T);
    printf("  %-40s %d\n", "Max keys per node (2t-1):",     MAX_KEYS);
    printf("  %-40s %d\n", "Min keys per non-root node (t-1):", T-1);
    printf("  %-40s %d\n", "Max children per node (2t):",   MAX_CH);
    printf("  %-40s %d\n", "Root (initially empty leaf):",  bt.root->n);
    printf("\n  B-Tree invariants:\n");
    printf("    I1. Keys within every node are stored in sorted order.\n");
    printf("    I2. All leaf nodes exist at the same depth.\n");
    printf("    I3. Every non-leaf node with n keys has exactly n+1 children.\n");
    printf("    I4. Each non-root node holds between %d and %d keys.\n", T-1, MAX_KEYS);
    printf("    I5. The root holds between 1 and %d keys (unless tree is empty).\n", MAX_KEYS);


    /* ── Task 2: Record Insertion ─────────────────────────────── */
    sep("TASK 2: Record Insertion");

    printf("\n  Inserting 5 keys into root (fills root to capacity %d):\n\n",
           MAX_KEYS);

    /* First batch: fills the root without causing a split */
    int batch1[] = {10, 20, 5, 6, 12};
    for (int i = 0; i < 5; i++) {
        printf("  ┌─ Insert %-3d ────────────────────────────────────────────\n",
               batch1[i]);
        char val[VAL_LEN];
        snprintf(val, VAL_LEN, "rec_%d", batch1[i]);
        btree_insert(&bt, batch1[i], val);
        printf("  └────────────────────────────────────────────────────────\n\n");
    }

    printf("  Root after batch 1 (keys in sorted order):\n");
    print_indent(bt.root, 1);
    printf("\n  Observation: keys are maintained in sorted order [5,6,10,12,20].\n");
    printf("  Root now holds %d/%d keys — capacity reached.\n", bt.root->n, MAX_KEYS);


    /* ── Task 3: Node Splitting ───────────────────────────────── */
    sep("TASK 3: Node Splitting");

    printf("\n  Inserting 10 more keys. Splits occur when nodes become full.\n\n");

    int batch2[] = {30, 7, 17, 3, 25, 35, 40, 2, 45, 50};
    for (int i = 0; i < 10; i++) {
        printf("  ┌─ Insert %-3d ────────────────────────────────────────────\n",
               batch2[i]);
        char val[VAL_LEN];
        snprintf(val, VAL_LEN, "rec_%d", batch2[i]);
        btree_insert(&bt, batch2[i], val);
        printf("  └────────────────────────────────────────────────────────\n\n");
    }

    printf("  Tree after all 15 insertions:\n\n");
    print_indent(bt.root, 1);

    printf("\n  Split summary:\n");
    printf("    Total splits performed : %d\n", bt.n_splits);
    printf("    Split #1 (key 30)  : root [5,6,10,12,20] full → median 10 promoted,\n");
    printf("                         left [5,6], right [12,20], new root [10].\n");
    printf("    Split #2 (key 35)  : child [12,17,20,25,30] full → median 20 promoted,\n");
    printf("                         left [12,17], right [25,30], root becomes [10,20].\n");
    printf("    Split #3 (key 50)  : child [25,30,35,40,45] full → median 35 promoted,\n");
    printf("                         left [25,30], right [40,45], root becomes [10,20,35].\n");
    printf("\n  After each split the tree height grows AT MOST by 1 (only root split raises height).\n");
    printf("  B-trees grow from the root upward — all leaves stay at the same depth.\n");


    /* ── Task 4: Search Operations ────────────────────────────── */
    sep("TASK 4: Search Operations");

    printf("\n  Searching 4 keys — path shows node accesses at each level.\n\n");

    int skeys[] = {17, 25, 11, 50};
    const char *expect[] = {"present", "present", "absent", "present"};
    int nskeys = (int)(sizeof skeys / sizeof skeys[0]);

    for (int i = 0; i < nskeys; i++) {
        printf("  ── Search key %-4d (%s) ──────────────────────────\n",
               skeys[i], expect[i]);
        btree_search(&bt, skeys[i]);
        printf("  ────────────────────────────────────────────────────────\n\n");
    }

    printf("  Search efficiency:\n");
    printf("    Tree height   = 1 (root + one leaf level)\n");
    printf("    Max node accesses per search = height + 1 = 2\n");
    printf("    B-tree height bound: log_t(n) = log_3(15) ≈ 2.5  →  at most 3 levels\n");
    printf("    Compare: unbalanced BST with 15 nodes → up to 15 comparisons.\n");


    /* ── Task 5: Tree Structure Analysis ─────────────────────── */
    sep("TASK 5: Tree Structure Analysis");

    printf("\n  Indented tree (each indent level = one tree level):\n\n");
    print_indent(bt.root, 1);

    printf("\n  Level-order layout:\n\n");
    print_levels(&bt);

    printf("\n  Statistics:\n");
    print_stats(&bt);

    printf("\n  Structure observations:\n");
    printf("    • Root [10, 20, 35] has 3 keys and 4 children.\n");
    printf("    • All 4 leaf nodes sit at depth 1 — perfectly balanced.\n");
    printf("    • Leaf [2,3,5,6,7] holds 5 keys (max capacity) — will split next.\n");
    printf("    • Leaf [12,17] and [25,30] hold 2 keys (min for non-root).\n");
    printf("    • The B-tree stays balanced without the explicit rotations\n");
    printf("      needed by AVL or Red-Black trees.\n");


    /* ── Task 6: Indexing Behaviour ──────────────────────────── */
    sep("TASK 6: Indexing Behaviour");

    printf("\n  In a database, each B-tree key maps to a record address:\n");
    printf("    key  → value (= page_id:slot in a real system)\n\n");

    /* Show key→value pairs in sorted order via inorder walk */
    /* Simple iterative inorder for leaf nodes */
    printf("  %-8s  %-20s  Notes\n", "Key", "Value (index entry)");
    printf("  %-8s  %-20s  -----\n", "---", "-------------------");

    /* Use level-order to collect all leaves, then print them */
    typedef struct { const Node *nd; int lv; } QE2;
    QE2 q2[512]; int h2 = 0, t2 = 0;
    q2[t2++] = (QE2){bt.root, 0};
    while (h2 < t2) {
        QE2 e = q2[h2++];
        if (e.nd->leaf) {
            for (int i = 0; i < e.nd->n; i++)
                printf("  %-8d  %-20s  depth=%d\n",
                       e.nd->keys[i], e.nd->vals[i], e.lv);
        } else {
            for (int i = 0; i <= e.nd->n; i++)
                q2[t2++] = (QE2){e.nd->ch[i], e.lv + 1};
        }
    }

    printf("\n  Database indexing observations:\n");
    printf("    1. ORDERED STORAGE: keys are kept sorted at every level,\n");
    printf("       enabling O(log n) point lookups AND efficient range scans.\n");
    printf("    2. NODE CAPACITY: storing up to %d keys per node drastically\n", MAX_KEYS);
    printf("       reduces tree height vs. a BST, minimising disk I/O in real\n");
    printf("       systems (one B-tree node typically fills one disk page).\n");
    printf("    3. BALANCED HEIGHT: all leaves at depth %d after %d inserts.\n",
           get_stats(bt.root, 0).height, bt.n_keys);
    printf("       A BST with the same keys in sorted order would have height 14.\n");
    printf("    4. SPLIT COST AMORTISED: %d splits for %d inserts = 1 split per\n",
           bt.n_splits, bt.n_keys);
    printf("       %.1f inserts on average — very low overhead.\n",
           (double)bt.n_keys / bt.n_splits);
    printf("    5. RANGE QUERIES: scan leaf nodes left-to-right for any range\n");
    printf("       [lo, hi] in O(log n + k) where k = number of matching keys.\n");
    printf("    6. USED IN: PostgreSQL (B+-tree for every index), InnoDB (B+-tree\n");
    printf("       clustered primary key), SQLite (B-tree pages in every table).\n");

    return 0;
}
