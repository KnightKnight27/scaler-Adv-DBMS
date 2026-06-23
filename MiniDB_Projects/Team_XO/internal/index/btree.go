// Package index implements a B+Tree used as MiniDB's ordered index structure.
// The tree maps a column value to the RID of the heap tuple that holds it.
//
// The implementation is an in-memory B+Tree with proper node splitting on
// insert and merge/redistribute on delete, and a linked list threading the leaf
// nodes for efficient range scans. Persisting nodes to pages is intentionally
// out of scope (see README "Limitations"); the engine rebuilds indexes from a
// heap scan at startup, so index durability rides on the heap's WAL rather than
// on logging the tree itself. Keeping the tree in memory lets the code focus on
// the B+Tree algorithm, which is what the project sets out to demonstrate.
package index

import (
	"minidb/internal/storage"
	"minidb/internal/types"
)

// entry is a (key, rid) pair. Internal nodes store separator entries (copies of
// the smallest entry in the right subtree); leaf nodes store real entries.
// Ordering is by key, breaking ties on RID, which makes every entry unique even
// when the indexed column has duplicate values. This single ordering supports
// both unique primary-key indexes and non-unique secondary indexes.
type entry struct {
	key types.Value
	rid storage.RID
}

func ridLess(a, b storage.RID) bool {
	if a.Page != b.Page {
		return a.Page < b.Page
	}
	return a.Slot < b.Slot
}

func compareEntry(a, b entry) int {
	if c := a.key.Compare(b.key); c != 0 {
		return c
	}
	switch {
	case ridLess(a.rid, b.rid):
		return -1
	case ridLess(b.rid, a.rid):
		return 1
	default:
		return 0
	}
}

type node struct {
	leaf     bool
	entries  []entry // separators (internal) or data entries (leaf)
	children []*node // internal only; len == len(entries)+1
	next     *node   // leaf chain only
}

// BPlusTree is a B+Tree keyed by entry ordering. order is the maximum number of
// children an internal node may have; a node overflows (and splits) once it
// would exceed order-1 entries.
type BPlusTree struct {
	root  *node
	order int
}

// New creates an empty tree with the given branching order (minimum 3).
func New(order int) *BPlusTree {
	if order < 3 {
		order = 3
	}
	return &BPlusTree{root: &node{leaf: true}, order: order}
}

func (t *BPlusTree) maxEntries() int { return t.order - 1 }
func (t *BPlusTree) minEntries() int { return (t.order - 1) / 2 }

// search returns the leaf-local index of the first entry >= target within n,
// using binary search over the sorted entries.
func (n *node) lowerBound(target entry) int {
	lo, hi := 0, len(n.entries)
	for lo < hi {
		mid := (lo + hi) / 2
		if compareEntry(n.entries[mid], target) < 0 {
			lo = mid + 1
		} else {
			hi = mid
		}
	}
	return lo
}

// Insert adds (key, rid). Duplicate (key, rid) pairs are ignored so the index
// stays a set.
func (t *BPlusTree) Insert(key types.Value, rid storage.RID) {
	e := entry{key: key, rid: rid}
	promoted, right, split := t.insert(t.root, e)
	if split {
		t.root = &node{
			leaf:     false,
			entries:  []entry{promoted},
			children: []*node{t.root, right},
		}
	}
}

func (t *BPlusTree) insert(n *node, e entry) (entry, *node, bool) {
	if n.leaf {
		i := n.lowerBound(e)
		if i < len(n.entries) && compareEntry(n.entries[i], e) == 0 {
			return entry{}, nil, false // already present
		}
		n.entries = append(n.entries, entry{})
		copy(n.entries[i+1:], n.entries[i:])
		n.entries[i] = e
		if len(n.entries) <= t.maxEntries() {
			return entry{}, nil, false
		}
		return t.splitLeaf(n)
	}

	i := n.lowerBound(e)
	// Descend into the child whose subtree may contain e. lowerBound gives the
	// first separator >= e; the matching child is at that same index.
	if i < len(n.entries) && compareEntry(n.entries[i], e) == 0 {
		i++
	}
	promoted, right, split := t.insert(n.children[i], e)
	if !split {
		return entry{}, nil, false
	}
	n.entries = append(n.entries, entry{})
	copy(n.entries[i+1:], n.entries[i:])
	n.entries[i] = promoted
	n.children = append(n.children, nil)
	copy(n.children[i+2:], n.children[i+1:])
	n.children[i+1] = right
	if len(n.entries) <= t.maxEntries() {
		return entry{}, nil, false
	}
	return t.splitInternal(n)
}

func (t *BPlusTree) splitLeaf(n *node) (entry, *node, bool) {
	mid := len(n.entries) / 2
	right := &node{leaf: true}
	right.entries = append(right.entries, n.entries[mid:]...)
	n.entries = n.entries[:mid]
	right.next = n.next
	n.next = right
	// Copy up the first key of the right leaf as the separator.
	return right.entries[0], right, true
}

func (t *BPlusTree) splitInternal(n *node) (entry, *node, bool) {
	mid := len(n.entries) / 2
	promoted := n.entries[mid]
	right := &node{leaf: false}
	right.entries = append(right.entries, n.entries[mid+1:]...)
	right.children = append(right.children, n.children[mid+1:]...)
	n.entries = n.entries[:mid]
	n.children = n.children[:mid+1]
	return promoted, right, true
}

// Search returns every RID whose indexed key equals key. For a unique index
// this is zero or one RID; for a secondary index it may be several.
func (t *BPlusTree) Search(key types.Value) []storage.RID {
	leaf, i := t.findLeaf(entry{key: key, rid: storage.RID{Page: minPage}})
	var out []storage.RID
	for leaf != nil {
		for ; i < len(leaf.entries); i++ {
			c := leaf.entries[i].key.Compare(key)
			if c < 0 {
				continue
			}
			if c > 0 {
				return out
			}
			out = append(out, leaf.entries[i].rid)
		}
		leaf = leaf.next
		i = 0
	}
	return out
}

// SearchRange returns the RIDs of all entries whose key is in [low, high]
// inclusive, in ascending key order, walking the leaf chain. This is the access
// path an index range scan uses.
func (t *BPlusTree) SearchRange(low, high types.Value) []storage.RID {
	leaf, i := t.findLeaf(entry{key: low, rid: storage.RID{Page: minPage}})
	var out []storage.RID
	for leaf != nil {
		for ; i < len(leaf.entries); i++ {
			if leaf.entries[i].key.Compare(high) > 0 {
				return out
			}
			out = append(out, leaf.entries[i].rid)
		}
		leaf = leaf.next
		i = 0
	}
	return out
}

const minPage storage.PageID = -1 << 62

// findLeaf descends to the leaf that would contain target and returns it with
// the lower-bound index inside it.
func (t *BPlusTree) findLeaf(target entry) (*node, int) {
	n := t.root
	for !n.leaf {
		i := n.lowerBound(target)
		if i < len(n.entries) && compareEntry(n.entries[i], target) == 0 {
			i++
		}
		n = n.children[i]
	}
	return n, n.lowerBound(target)
}

// Delete removes (key, rid) and rebalances. It returns true if the pair was
// found and removed.
func (t *BPlusTree) Delete(key types.Value, rid storage.RID) bool {
	removed := t.delete(t.root, entry{key: key, rid: rid})
	// Collapse a non-leaf root left with a single child.
	if !t.root.leaf && len(t.root.entries) == 0 {
		t.root = t.root.children[0]
	}
	return removed
}

func (t *BPlusTree) delete(n *node, e entry) bool {
	if n.leaf {
		i := n.lowerBound(e)
		if i >= len(n.entries) || compareEntry(n.entries[i], e) != 0 {
			return false
		}
		n.entries = append(n.entries[:i], n.entries[i+1:]...)
		return true
	}

	i := n.lowerBound(e)
	if i < len(n.entries) && compareEntry(n.entries[i], e) == 0 {
		i++
	}
	removed := t.delete(n.children[i], e)
	if !removed {
		return false
	}
	if t.underflow(n.children[i]) {
		t.rebalance(n, i)
	}
	return true
}

func (t *BPlusTree) underflow(n *node) bool {
	return len(n.entries) < t.minEntries()
}

// rebalance restores the minimum-occupancy invariant for child i of n by
// borrowing from a sibling (redistribute) when possible, otherwise merging with
// a sibling.
func (t *BPlusTree) rebalance(parent *node, i int) {
	child := parent.children[i]

	if i > 0 && len(parent.children[i-1].entries) > t.minEntries() {
		t.borrowFromLeft(parent, i)
		return
	}
	if i < len(parent.children)-1 && len(parent.children[i+1].entries) > t.minEntries() {
		t.borrowFromRight(parent, i)
		return
	}
	if i > 0 {
		t.merge(parent, i-1) // merge child into its left sibling
	} else {
		t.merge(parent, i) // merge right sibling into child
	}
	_ = child
}

func (t *BPlusTree) borrowFromLeft(parent *node, i int) {
	child := parent.children[i]
	left := parent.children[i-1]
	if child.leaf {
		moved := left.entries[len(left.entries)-1]
		left.entries = left.entries[:len(left.entries)-1]
		child.entries = append([]entry{moved}, child.entries...)
		parent.entries[i-1] = child.entries[0]
	} else {
		child.entries = append([]entry{parent.entries[i-1]}, child.entries...)
		parent.entries[i-1] = left.entries[len(left.entries)-1]
		left.entries = left.entries[:len(left.entries)-1]
		movedChild := left.children[len(left.children)-1]
		left.children = left.children[:len(left.children)-1]
		child.children = append([]*node{movedChild}, child.children...)
	}
}

func (t *BPlusTree) borrowFromRight(parent *node, i int) {
	child := parent.children[i]
	right := parent.children[i+1]
	if child.leaf {
		moved := right.entries[0]
		right.entries = right.entries[1:]
		child.entries = append(child.entries, moved)
		parent.entries[i] = right.entries[0]
	} else {
		child.entries = append(child.entries, parent.entries[i])
		parent.entries[i] = right.entries[0]
		right.entries = right.entries[1:]
		movedChild := right.children[0]
		right.children = right.children[1:]
		child.children = append(child.children, movedChild)
	}
}

// merge folds children[i+1] into children[i], pulling down the separator at i
// for internal nodes, then drops the now-empty slot from the parent.
func (t *BPlusTree) merge(parent *node, i int) {
	left := parent.children[i]
	right := parent.children[i+1]
	if left.leaf {
		left.entries = append(left.entries, right.entries...)
		left.next = right.next
	} else {
		left.entries = append(left.entries, parent.entries[i])
		left.entries = append(left.entries, right.entries...)
		left.children = append(left.children, right.children...)
	}
	parent.entries = append(parent.entries[:i], parent.entries[i+1:]...)
	parent.children = append(parent.children[:i+1], parent.children[i+2:]...)
}
