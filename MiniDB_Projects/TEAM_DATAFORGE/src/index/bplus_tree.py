"""
B+ Tree — Disk-aware B+ Tree index for MiniDB.

This is an in-memory B+ Tree implementation that maps key values to RIDs
(Record Identifiers). It supports:
  - Point lookups (search)
  - Range scans
  - Insertions with node splitting
  - Deletions with redistribution/merging
  - Leaf-level linked list for efficient range queries

The tree maintains sorted order and guarantees O(log n) search/insert/delete.

Design choices:
  - In-memory for simplicity (pages could be disk-backed via buffer pool)
  - Keys are comparable Python values (int, float, str)
  - Values are RID tuples (page_id, slot_id)
  - Configurable order (max children per internal node)
"""

from typing import Optional
from src.storage.heap_file import RID


class BPlusNode:
    """Base class for B+ tree nodes."""

    def __init__(self, is_leaf: bool = False):
        self.is_leaf = is_leaf
        self.keys = []
        self.parent = None


class BPlusLeaf(BPlusNode):
    """
    Leaf node — stores (key, RID) pairs and links to siblings.

    Attributes:
        keys: Sorted list of key values.
        values: Corresponding list of RIDs (parallel to keys).
        next_leaf: Pointer to the next leaf (for range scans).
        prev_leaf: Pointer to the previous leaf.
    """

    def __init__(self):
        super().__init__(is_leaf=True)
        self.values = []         # List of RIDs, parallel to keys
        self.next_leaf: Optional['BPlusLeaf'] = None
        self.prev_leaf: Optional['BPlusLeaf'] = None


class BPlusInternal(BPlusNode):
    """
    Internal node — stores keys and child pointers.

    For n keys, there are n+1 children.
    children[i] contains keys < keys[i]
    children[i+1] contains keys >= keys[i]
    """

    def __init__(self):
        super().__init__(is_leaf=False)
        self.children = []  # List of BPlusNode


class BPlusTree:
    """
    B+ Tree index implementation.

    Supports point lookups, range queries, insertion, and deletion.
    Uses leaf-level linked list for efficient range scans.

    Args:
        order: Maximum number of children per internal node.
               Leaf nodes store at most (order - 1) keys.

    Usage:
        tree = BPlusTree(order=4)
        tree.insert(10, RID(1, 0))
        tree.insert(20, RID(1, 1))
        result = tree.search(10)  # Returns RID(1, 0)
        range_results = tree.range_search(5, 25)  # Returns [(10, RID), (20, RID)]
    """

    def __init__(self, order: int = 50):
        if order < 3:
            raise ValueError("B+ Tree order must be at least 3")
        self.order = order
        self.root: Optional[BPlusNode] = None
        self._size = 0
        self._height = 0

    @property
    def max_keys(self):
        """Maximum keys per node (order - 1)."""
        return self.order - 1

    @property
    def min_keys(self):
        """Minimum keys per non-root node."""
        return (self.order - 1) // 2

    # ─── Search ───────────────────────────────────────────────────────

    def search(self, key) -> Optional[RID]:
        """
        Search for a key and return its RID.

        Args:
            key: The key to search for.

        Returns:
            The RID associated with the key, or None if not found.
        """
        if self.root is None:
            return None

        leaf = self._find_leaf(key)
        for i, k in enumerate(leaf.keys):
            if k == key:
                return leaf.values[i]
        return None

    def search_all(self, key) -> list:
        """Search for all RIDs with the given key (for non-unique indexes)."""
        results = []
        if self.root is None:
            return results

        leaf = self._find_leaf(key)
        # Scan through leaves
        while leaf is not None:
            for i, k in enumerate(leaf.keys):
                if k == key:
                    results.append(leaf.values[i])
                elif k > key:
                    return results
            leaf = leaf.next_leaf
        return results

    def range_search(self, low, high, inclusive_low=True, inclusive_high=True) -> list:
        """
        Range scan: find all (key, RID) pairs where low <= key <= high.

        Args:
            low: Lower bound of the range.
            high: Upper bound of the range.
            inclusive_low: Include low boundary.
            inclusive_high: Include high boundary.

        Returns:
            List of (key, RID) tuples in sorted order.
        """
        results = []
        if self.root is None:
            return results

        leaf = self._find_leaf(low)

        while leaf is not None:
            for i, k in enumerate(leaf.keys):
                if inclusive_low and k < low:
                    continue
                if not inclusive_low and k <= low:
                    continue
                if inclusive_high and k > high:
                    return results
                if not inclusive_high and k >= high:
                    return results
                results.append((k, leaf.values[i]))
            leaf = leaf.next_leaf

        return results

    def scan_all(self) -> list:
        """
        Scan all entries in sorted order.

        Returns:
            List of (key, RID) tuples.
        """
        results = []
        if self.root is None:
            return results

        # Find leftmost leaf
        node = self.root
        while not node.is_leaf:
            node = node.children[0]

        leaf = node
        while leaf is not None:
            for i, k in enumerate(leaf.keys):
                results.append((k, leaf.values[i]))
            leaf = leaf.next_leaf

        return results

    def _find_leaf(self, key) -> BPlusLeaf:
        """Navigate from root to the appropriate leaf node."""
        node = self.root
        while not node.is_leaf:
            # Find child to descend into
            i = 0
            while i < len(node.keys) and key >= node.keys[i]:
                i += 1
            node = node.children[i]
        return node

    # ─── Insert ──────────────────────────────────────────────────────

    def insert(self, key, rid: RID):
        """
        Insert a key-RID pair into the B+ tree.

        Args:
            key: The key value.
            rid: The Record Identifier.
        """
        if self.root is None:
            # Create first leaf
            leaf = BPlusLeaf()
            leaf.keys.append(key)
            leaf.values.append(rid)
            self.root = leaf
            self._size = 1
            self._height = 1
            return

        leaf = self._find_leaf(key)
        self._insert_into_leaf(leaf, key, rid)
        self._size += 1

    def _insert_into_leaf(self, leaf: BPlusLeaf, key, rid: RID):
        """Insert a key-value into a leaf, splitting if necessary."""
        # Find insertion position (maintain sorted order)
        i = 0
        while i < len(leaf.keys) and leaf.keys[i] < key:
            i += 1

        leaf.keys.insert(i, key)
        leaf.values.insert(i, rid)

        # Check if overflow
        if len(leaf.keys) > self.max_keys:
            self._split_leaf(leaf)

    def _split_leaf(self, leaf: BPlusLeaf):
        """Split an overflowing leaf node."""
        mid = len(leaf.keys) // 2

        # Create new leaf with right half
        new_leaf = BPlusLeaf()
        new_leaf.keys = leaf.keys[mid:]
        new_leaf.values = leaf.values[mid:]

        # Truncate original leaf to left half
        leaf.keys = leaf.keys[:mid]
        leaf.values = leaf.values[:mid]

        # Update linked list
        new_leaf.next_leaf = leaf.next_leaf
        new_leaf.prev_leaf = leaf
        if leaf.next_leaf:
            leaf.next_leaf.prev_leaf = new_leaf
        leaf.next_leaf = new_leaf

        # Promote the first key of new_leaf to parent
        promote_key = new_leaf.keys[0]
        self._insert_into_parent(leaf, promote_key, new_leaf)

    def _insert_into_parent(self, left_node, key, right_node):
        """Insert a key and right child into the parent of left_node."""
        if left_node.parent is None:
            # Create new root
            new_root = BPlusInternal()
            new_root.keys = [key]
            new_root.children = [left_node, right_node]
            left_node.parent = new_root
            right_node.parent = new_root
            self.root = new_root
            self._height += 1
            return

        parent = left_node.parent

        # Find position of left_node in parent's children
        idx = parent.children.index(left_node)

        # Insert key and right child
        parent.keys.insert(idx, key)
        parent.children.insert(idx + 1, right_node)
        right_node.parent = parent

        # Check if parent overflows
        if len(parent.keys) > self.max_keys:
            self._split_internal(parent)

    def _split_internal(self, node: BPlusInternal):
        """Split an overflowing internal node."""
        mid = len(node.keys) // 2
        promote_key = node.keys[mid]

        # Create new internal node with right half
        new_node = BPlusInternal()
        new_node.keys = node.keys[mid + 1:]
        new_node.children = node.children[mid + 1:]

        # Update children's parent pointers
        for child in new_node.children:
            child.parent = new_node

        # Truncate original to left half
        node.keys = node.keys[:mid]
        node.children = node.children[:mid + 1]

        # Promote middle key to parent
        self._insert_into_parent(node, promote_key, new_node)

    # ─── Delete ──────────────────────────────────────────────────────

    def delete(self, key) -> bool:
        """
        Delete a key from the B+ tree.

        Args:
            key: The key to delete.

        Returns:
            True if the key was found and deleted, False otherwise.
        """
        if self.root is None:
            return False

        leaf = self._find_leaf(key)

        # Find the key in the leaf
        try:
            idx = leaf.keys.index(key)
        except ValueError:
            return False

        leaf.keys.pop(idx)
        leaf.values.pop(idx)
        self._size -= 1

        # If root is a leaf, no underflow handling needed
        if leaf == self.root:
            if len(leaf.keys) == 0:
                self.root = None
                self._height = 0
            return True

        # Check for underflow
        if len(leaf.keys) < self.min_keys:
            self._handle_leaf_underflow(leaf)

        return True

    def _handle_leaf_underflow(self, leaf: BPlusLeaf):
        """Handle underflow in a leaf node via redistribution or merging."""
        parent = leaf.parent
        if parent is None:
            return

        idx = parent.children.index(leaf)

        # Try to borrow from left sibling
        if idx > 0:
            left_sib = parent.children[idx - 1]
            if len(left_sib.keys) > self.min_keys:
                # Borrow last key from left sibling
                leaf.keys.insert(0, left_sib.keys.pop())
                leaf.values.insert(0, left_sib.values.pop())
                parent.keys[idx - 1] = leaf.keys[0]
                return

        # Try to borrow from right sibling
        if idx < len(parent.children) - 1:
            right_sib = parent.children[idx + 1]
            if len(right_sib.keys) > self.min_keys:
                leaf.keys.append(right_sib.keys.pop(0))
                leaf.values.append(right_sib.values.pop(0))
                parent.keys[idx] = right_sib.keys[0]
                return

        # Merge with a sibling
        if idx > 0:
            # Merge with left sibling
            left_sib = parent.children[idx - 1]
            left_sib.keys.extend(leaf.keys)
            left_sib.values.extend(leaf.values)
            left_sib.next_leaf = leaf.next_leaf
            if leaf.next_leaf:
                leaf.next_leaf.prev_leaf = left_sib
            # Remove the key and child from parent
            parent.keys.pop(idx - 1)
            parent.children.pop(idx)
        else:
            # Merge with right sibling
            right_sib = parent.children[idx + 1]
            leaf.keys.extend(right_sib.keys)
            leaf.values.extend(right_sib.values)
            leaf.next_leaf = right_sib.next_leaf
            if right_sib.next_leaf:
                right_sib.next_leaf.prev_leaf = leaf
            parent.keys.pop(idx)
            parent.children.pop(idx + 1)

        # Check parent for underflow
        if parent == self.root:
            if len(parent.keys) == 0:
                self.root = parent.children[0]
                self.root.parent = None
                self._height -= 1
        elif len(parent.keys) < self.min_keys:
            self._handle_internal_underflow(parent)

    def _handle_internal_underflow(self, node: BPlusInternal):
        """Handle underflow in an internal node."""
        parent = node.parent
        if parent is None:
            return

        idx = parent.children.index(node)

        # Try to borrow from left sibling
        if idx > 0:
            left_sib = parent.children[idx - 1]
            if len(left_sib.keys) > self.min_keys:
                node.keys.insert(0, parent.keys[idx - 1])
                parent.keys[idx - 1] = left_sib.keys.pop()
                child = left_sib.children.pop()
                child.parent = node
                node.children.insert(0, child)
                return

        # Try to borrow from right sibling
        if idx < len(parent.children) - 1:
            right_sib = parent.children[idx + 1]
            if len(right_sib.keys) > self.min_keys:
                node.keys.append(parent.keys[idx])
                parent.keys[idx] = right_sib.keys.pop(0)
                child = right_sib.children.pop(0)
                child.parent = node
                node.children.append(child)
                return

        # Merge
        if idx > 0:
            left_sib = parent.children[idx - 1]
            left_sib.keys.append(parent.keys.pop(idx - 1))
            left_sib.keys.extend(node.keys)
            left_sib.children.extend(node.children)
            for child in node.children:
                child.parent = left_sib
            parent.children.pop(idx)
        else:
            right_sib = parent.children[idx + 1]
            node.keys.append(parent.keys.pop(idx))
            node.keys.extend(right_sib.keys)
            node.children.extend(right_sib.children)
            for child in right_sib.children:
                child.parent = node
            parent.children.pop(idx + 1)

        if parent == self.root and len(parent.keys) == 0:
            self.root = parent.children[0]
            self.root.parent = None
            self._height -= 1
        elif parent != self.root and len(parent.keys) < self.min_keys:
            self._handle_internal_underflow(parent)

    # ─── Utilities ───────────────────────────────────────────────────

    def __len__(self):
        return self._size

    def __contains__(self, key):
        return self.search(key) is not None

    @property
    def height(self):
        return self._height

    def is_empty(self):
        return self.root is None

    def get_min_key(self):
        """Get the minimum key in the tree."""
        if self.root is None:
            return None
        node = self.root
        while not node.is_leaf:
            node = node.children[0]
        return node.keys[0] if node.keys else None

    def get_max_key(self):
        """Get the maximum key in the tree."""
        if self.root is None:
            return None
        node = self.root
        while not node.is_leaf:
            node = node.children[-1]
        return node.keys[-1] if node.keys else None

    def print_tree(self, node=None, level=0):
        """Print the tree structure for debugging."""
        if node is None:
            node = self.root
        if node is None:
            print("(empty tree)")
            return

        indent = "  " * level
        if node.is_leaf:
            pairs = list(zip(node.keys, node.values))
            print(f"{indent}Leaf: {pairs}")
        else:
            print(f"{indent}Internal: keys={node.keys}")
            for child in node.children:
                self.print_tree(child, level + 1)

    def __repr__(self):
        return f"BPlusTree(order={self.order}, size={self._size}, height={self._height})"
