package minidb.index;

import minidb.common.Types.RID;
import java.util.*;

/**
 * A B+ Tree mapping integer keys -> RID (record location).
 *
 * Properties of a B+ tree (and why databases use it):
 *   - All real data lives in LEAF nodes; internal nodes only route searches.
 *   - Leaves are linked left-to-right, so a range scan is a single sequential
 *     walk after one search to find the start.
 *   - The tree stays balanced: every root-to-leaf path is the same length,
 *     giving O(log n) search/insert/delete.
 *
 * "order" = max children per internal node. A node splits when it overflows.
 *
 * This is an in-memory B+ tree (rebuilt from the heap file on startup). That is
 * a deliberate, defensible simplification: it keeps the index logic clear while
 * still demonstrating real B+ tree mechanics, splits, and index-vs-scan choice.
 */
public final class BPlusTree {
    private final int order;
    private Node root;
    public long lookups = 0; // stat for benchmarking

    public BPlusTree() { this(64); }
    public BPlusTree(int order) {
        this.order = order;
        this.root = new Node(true);
    }

    private final class Node {
        boolean leaf;
        List<Integer> keys = new ArrayList<>();
        List<Node> children = new ArrayList<>();   // internal nodes
        List<RID> values = new ArrayList<>();        // leaf nodes
        Node next;                                    // leaf chain
        Node(boolean leaf) { this.leaf = leaf; }
    }

    // ---------- SEARCH ----------
    public RID search(int key) {
        lookups++;
        Node n = root;
        while (!n.leaf) n = n.children.get(childIndex(n, key));
        int i = Collections.binarySearch(n.keys, key);
        return (i >= 0) ? n.values.get(i) : null;
    }

    /** Range scan [lo, hi] inclusive, in key order. */
    public List<RID> rangeScan(int lo, int hi) {
        List<RID> out = new ArrayList<>();
        Node n = root;
        while (!n.leaf) n = n.children.get(childIndex(n, lo));
        while (n != null) {
            for (int i = 0; i < n.keys.size(); i++) {
                int k = n.keys.get(i);
                if (k > hi) return out;
                if (k >= lo) out.add(n.values.get(i));
            }
            n = n.next;
        }
        return out;
    }

    private int childIndex(Node n, int key) {
        int i = 0;
        while (i < n.keys.size() && key >= n.keys.get(i)) i++;
        return i;
    }

    // ---------- INSERT ----------
    public void insert(int key, RID rid) {
        Split s = insert(root, key, rid);
        if (s != null) {
            // root split: build a new root one level up
            Node newRoot = new Node(false);
            newRoot.keys.add(s.key);
            newRoot.children.add(root);
            newRoot.children.add(s.right);
            root = newRoot;
        }
    }

    private final class Split { int key; Node right; Split(int k, Node r){key=k;right=r;} }

    private Split insert(Node n, int key, RID rid) {
        if (n.leaf) {
            int i = Collections.binarySearch(n.keys, key);
            if (i >= 0) { n.values.set(i, rid); return null; } // overwrite duplicate key
            int pos = -i - 1;
            n.keys.add(pos, key);
            n.values.add(pos, rid);
            if (n.keys.size() < order) return null;
            return splitLeaf(n);
        } else {
            int ci = childIndex(n, key);
            Split s = insert(n.children.get(ci), key, rid);
            if (s == null) return null;
            int pos = childIndex(n, s.key);
            n.keys.add(pos, s.key);
            n.children.add(pos + 1, s.right);
            if (n.children.size() <= order) return null;
            return splitInternal(n);
        }
    }

    private Split splitLeaf(Node n) {
        int mid = n.keys.size() / 2;
        Node right = new Node(true);
        right.keys.addAll(n.keys.subList(mid, n.keys.size()));
        right.values.addAll(n.values.subList(mid, n.values.size()));
        n.keys.subList(mid, n.keys.size()).clear();
        n.values.subList(mid, n.values.size()).clear();
        right.next = n.next;
        n.next = right;
        return new Split(right.keys.get(0), right); // copy-up
    }

    private Split splitInternal(Node n) {
        int mid = n.keys.size() / 2;
        int upKey = n.keys.get(mid);
        Node right = new Node(false);
        right.keys.addAll(n.keys.subList(mid + 1, n.keys.size()));
        right.children.addAll(n.children.subList(mid + 1, n.children.size()));
        n.keys.subList(mid, n.keys.size()).clear();
        n.children.subList(mid + 1, n.children.size()).clear();
        return new Split(upKey, right); // push-up
    }

    // ---------- DELETE ----------
    // Simplified delete: remove the key from its leaf. We do not merge
    // underflowing nodes (a documented simplification). Correctness of lookups
    // is preserved; the tree may just be slightly less compact after many deletes.
    public boolean delete(int key) {
        Node n = root;
        while (!n.leaf) n = n.children.get(childIndex(n, key));
        int i = Collections.binarySearch(n.keys, key);
        if (i < 0) return false;
        n.keys.remove(i);
        n.values.remove(i);
        return true;
    }

    public int height() {
        int h = 1; Node n = root;
        while (!n.leaf) { n = n.children.get(0); h++; }
        return h;
    }
}
