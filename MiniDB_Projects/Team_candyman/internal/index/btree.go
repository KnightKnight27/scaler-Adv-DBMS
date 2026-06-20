// Package index implements a disk-backed B+Tree over the storage buffer pool.
//
// The tree maps an encoded primary-key (see types.EncodeKey, which is
// order-preserving so bytes.Compare matches value order) to a 6-byte RID in the
// heap file. One B+Tree node occupies one 4 KiB index page; nodes split when
// their serialized form would overflow a page, giving a variable fan-out that
// adapts to key size.
//
// Trade-off: deletes are "lazy" — an entry is removed from its leaf but nodes are
// never merged or rebalanced. Searches stay correct and this avoids the famously
// bug-prone B+Tree merge logic. Because MiniDB rebuilds every index from the heap
// at startup, occasional under-full nodes never accumulate across runs.
package index

import (
	"bytes"
	"encoding/binary"
	"fmt"

	"minidb/internal/storage"
	"minidb/internal/types"
)

const (
	nodeHeaderSize = 7 // flags(1) + keyCount(2) + nextLeaf/unused(4)
	flagLeaf       = 1
)

// BTree is a primary-key index rooted at root in the index file.
type BTree struct {
	bp   *storage.BufferPool
	root storage.PageID
}

// node is the decoded in-memory form of an index page.
type node struct {
	leaf     bool
	keys     [][]byte
	vals     [][]byte          // leaf only: encoded RIDs
	next     storage.PageID    // leaf only: next leaf for range scans
	children []storage.PageID  // internal only: len == len(keys)+1
}

// NewBTree creates an empty tree (a single empty leaf root).
func NewBTree(bp *storage.BufferPool) (*BTree, error) {
	rootID, _, err := bp.NewPage(storage.FileIndex)
	if err != nil {
		return nil, err
	}
	t := &BTree{bp: bp, root: rootID}
	if err := t.save(rootID, &node{leaf: true, next: storage.InvalidPageID}); err != nil {
		bp.Unpin(storage.FileIndex, rootID, true)
		return nil, err
	}
	bp.Unpin(storage.FileIndex, rootID, true)
	return t, nil
}

// Root returns the current root page id.
func (t *BTree) Root() storage.PageID { return t.root }

func (t *BTree) load(pid storage.PageID) (*node, error) {
	p, err := t.bp.Fetch(storage.FileIndex, pid)
	if err != nil {
		return nil, err
	}
	defer t.bp.Unpin(storage.FileIndex, pid, false)
	return decodeNode(p.Bytes()), nil
}

func (t *BTree) save(pid storage.PageID, n *node) error {
	p, err := t.bp.Fetch(storage.FileIndex, pid)
	if err != nil {
		return err
	}
	defer t.bp.Unpin(storage.FileIndex, pid, true)
	buf := encodeNode(n)
	if len(buf) > storage.PageSize {
		return fmt.Errorf("btree: node serialization %d exceeds page size", len(buf))
	}
	dst := p.Bytes()
	copy(dst, buf)
	return nil
}

func encodeNode(n *node) []byte {
	var b bytes.Buffer
	var flags byte
	if n.leaf {
		flags = flagLeaf
	}
	b.WriteByte(flags)
	var tmp [4]byte
	binary.BigEndian.PutUint16(tmp[:2], uint16(len(n.keys)))
	b.Write(tmp[:2])
	if n.leaf {
		binary.BigEndian.PutUint32(tmp[:], uint32(n.next))
		b.Write(tmp[:])
		for i, k := range n.keys {
			binary.BigEndian.PutUint16(tmp[:2], uint16(len(k)))
			b.Write(tmp[:2])
			b.Write(k)
			b.Write(n.vals[i]) // 6-byte RID
		}
	} else {
		b.Write(tmp[:]) // unused 4 bytes
		binary.BigEndian.PutUint32(tmp[:], uint32(n.children[0]))
		b.Write(tmp[:])
		for i, k := range n.keys {
			binary.BigEndian.PutUint16(tmp[:2], uint16(len(k)))
			b.Write(tmp[:2])
			b.Write(k)
			binary.BigEndian.PutUint32(tmp[:], uint32(n.children[i+1]))
			b.Write(tmp[:])
		}
	}
	return b.Bytes()
}

func decodeNode(data []byte) *node {
	n := &node{leaf: data[0]&flagLeaf != 0}
	cnt := int(binary.BigEndian.Uint16(data[1:3]))
	off := nodeHeaderSize
	if n.leaf {
		n.next = storage.PageID(binary.BigEndian.Uint32(data[3:7]))
		for i := 0; i < cnt; i++ {
			kl := int(binary.BigEndian.Uint16(data[off : off+2]))
			off += 2
			key := append([]byte(nil), data[off:off+kl]...)
			off += kl
			val := append([]byte(nil), data[off:off+6]...)
			off += 6
			n.keys = append(n.keys, key)
			n.vals = append(n.vals, val)
		}
	} else {
		c0 := storage.PageID(binary.BigEndian.Uint32(data[off : off+4]))
		off += 4
		n.children = append(n.children, c0)
		for i := 0; i < cnt; i++ {
			kl := int(binary.BigEndian.Uint16(data[off : off+2]))
			off += 2
			key := append([]byte(nil), data[off:off+kl]...)
			off += kl
			ch := storage.PageID(binary.BigEndian.Uint32(data[off : off+4]))
			off += 4
			n.keys = append(n.keys, key)
			n.children = append(n.children, ch)
		}
	}
	return n
}

func nodeSize(n *node) int {
	sz := nodeHeaderSize
	if n.leaf {
		for _, k := range n.keys {
			sz += 2 + len(k) + 6
		}
	} else {
		sz += 4 // child0
		for _, k := range n.keys {
			sz += 2 + len(k) + 4
		}
	}
	return sz
}

// findLeafPos returns the index of key in a leaf, and whether it was found.
func findLeafPos(n *node, key []byte) (int, bool) {
	lo, hi := 0, len(n.keys)
	for lo < hi {
		mid := (lo + hi) / 2
		switch bytes.Compare(n.keys[mid], key) {
		case 0:
			return mid, true
		case -1:
			lo = mid + 1
		default:
			hi = mid
		}
	}
	return lo, false
}

// childIndex returns which child to descend into for key in an internal node.
func childIndex(n *node, key []byte) int {
	i := 0
	for i < len(n.keys) && bytes.Compare(key, n.keys[i]) >= 0 {
		i++
	}
	return i
}

// Insert upserts key -> rid. Existing keys are overwritten (idempotent for redo).
func (t *BTree) Insert(key types.Value, rid storage.RID) error {
	ek := types.EncodeKey(key)
	split, sepKey, rightPID, err := t.insert(t.root, ek, rid.Encode())
	if err != nil {
		return err
	}
	if split {
		newRootID, _, err := t.bp.NewPage(storage.FileIndex)
		if err != nil {
			return err
		}
		newRoot := &node{
			leaf:     false,
			keys:     [][]byte{sepKey},
			children: []storage.PageID{t.root, rightPID},
		}
		if err := t.save(newRootID, newRoot); err != nil {
			t.bp.Unpin(storage.FileIndex, newRootID, true)
			return err
		}
		t.bp.Unpin(storage.FileIndex, newRootID, true)
		t.root = newRootID
	}
	return nil
}

func (t *BTree) insert(pid storage.PageID, key, val []byte) (bool, []byte, storage.PageID, error) {
	n, err := t.load(pid)
	if err != nil {
		return false, nil, 0, err
	}
	if n.leaf {
		pos, found := findLeafPos(n, key)
		if found {
			n.vals[pos] = val // upsert
		} else {
			n.keys = append(n.keys, nil)
			copy(n.keys[pos+1:], n.keys[pos:])
			n.keys[pos] = key
			n.vals = append(n.vals, nil)
			copy(n.vals[pos+1:], n.vals[pos:])
			n.vals[pos] = val
		}
		if nodeSize(n) <= storage.PageSize {
			return false, nil, 0, t.save(pid, n)
		}
		return t.splitLeaf(pid, n)
	}

	ci := childIndex(n, key)
	split, sepKey, rightPID, err := t.insert(n.children[ci], key, val)
	if err != nil {
		return false, nil, 0, err
	}
	if !split {
		return false, nil, 0, nil
	}
	// insert separator + new right child
	n.keys = append(n.keys, nil)
	copy(n.keys[ci+1:], n.keys[ci:])
	n.keys[ci] = sepKey
	n.children = append(n.children, 0)
	copy(n.children[ci+2:], n.children[ci+1:])
	n.children[ci+1] = rightPID
	if nodeSize(n) <= storage.PageSize {
		return false, nil, 0, t.save(pid, n)
	}
	return t.splitInternal(pid, n)
}

func (t *BTree) splitLeaf(pid storage.PageID, n *node) (bool, []byte, storage.PageID, error) {
	mid := len(n.keys) / 2
	rightID, _, err := t.bp.NewPage(storage.FileIndex)
	if err != nil {
		return false, nil, 0, err
	}
	t.bp.Unpin(storage.FileIndex, rightID, true)
	right := &node{
		leaf: true,
		keys: append([][]byte(nil), n.keys[mid:]...),
		vals: append([][]byte(nil), n.vals[mid:]...),
		next: n.next,
	}
	n.keys = n.keys[:mid]
	n.vals = n.vals[:mid]
	n.next = rightID
	if err := t.save(pid, n); err != nil {
		return false, nil, 0, err
	}
	if err := t.save(rightID, right); err != nil {
		return false, nil, 0, err
	}
	sep := append([]byte(nil), right.keys[0]...) // copy-up
	return true, sep, rightID, nil
}

func (t *BTree) splitInternal(pid storage.PageID, n *node) (bool, []byte, storage.PageID, error) {
	mid := len(n.keys) / 2
	sep := append([]byte(nil), n.keys[mid]...) // push-up (removed from children)
	rightID, _, err := t.bp.NewPage(storage.FileIndex)
	if err != nil {
		return false, nil, 0, err
	}
	t.bp.Unpin(storage.FileIndex, rightID, true)
	right := &node{
		leaf:     false,
		keys:     append([][]byte(nil), n.keys[mid+1:]...),
		children: append([]storage.PageID(nil), n.children[mid+1:]...),
	}
	n.keys = n.keys[:mid]
	n.children = n.children[:mid+1]
	if err := t.save(pid, n); err != nil {
		return false, nil, 0, err
	}
	if err := t.save(rightID, right); err != nil {
		return false, nil, 0, err
	}
	return true, sep, rightID, nil
}

// Search returns the RID for key, or ok=false.
func (t *BTree) Search(key types.Value) (storage.RID, bool, error) {
	ek := types.EncodeKey(key)
	pid := t.root
	for {
		n, err := t.load(pid)
		if err != nil {
			return storage.RID{}, false, err
		}
		if n.leaf {
			pos, found := findLeafPos(n, ek)
			if !found {
				return storage.RID{}, false, nil
			}
			return storage.DecodeRID(n.vals[pos]), true, nil
		}
		pid = n.children[childIndex(n, ek)]
	}
}

// Delete removes key from its leaf (lazy; no rebalancing). Reports if it existed.
func (t *BTree) Delete(key types.Value) (bool, error) {
	ek := types.EncodeKey(key)
	pid := t.root
	for {
		n, err := t.load(pid)
		if err != nil {
			return false, err
		}
		if n.leaf {
			pos, found := findLeafPos(n, ek)
			if !found {
				return false, nil
			}
			n.keys = append(n.keys[:pos], n.keys[pos+1:]...)
			n.vals = append(n.vals[:pos], n.vals[pos+1:]...)
			return true, t.save(pid, n)
		}
		pid = n.children[childIndex(n, ek)]
	}
}

// Range calls fn for every (key, RID) with low <= key <= high in ascending order.
// A nil low means "from the start"; a nil high means "to the end".
func (t *BTree) Range(low, high *types.Value, fn func(rid storage.RID) bool) error {
	var lo, hi []byte
	if low != nil {
		lo = types.EncodeKey(*low)
	}
	if high != nil {
		hi = types.EncodeKey(*high)
	}
	// descend to the leaf containing lo
	pid := t.root
	for {
		n, err := t.load(pid)
		if err != nil {
			return err
		}
		if n.leaf {
			return t.scanLeaves(pid, lo, hi, fn)
		}
		if lo == nil {
			pid = n.children[0]
		} else {
			pid = n.children[childIndex(n, lo)]
		}
	}
}

func (t *BTree) scanLeaves(pid storage.PageID, lo, hi []byte, fn func(storage.RID) bool) error {
	for pid != storage.InvalidPageID {
		n, err := t.load(pid)
		if err != nil {
			return err
		}
		for i, k := range n.keys {
			if lo != nil && bytes.Compare(k, lo) < 0 {
				continue
			}
			if hi != nil && bytes.Compare(k, hi) > 0 {
				return nil
			}
			if !fn(storage.DecodeRID(n.vals[i])) {
				return nil
			}
		}
		pid = n.next
	}
	return nil
}
