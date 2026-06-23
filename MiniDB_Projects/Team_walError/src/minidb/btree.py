"""btree.py — an in-memory B+ tree index mapping keys -> RID.

Why a B+ tree: it keeps keys sorted and balanced, giving O(log n) point lookups
AND efficient ordered range scans (all data lives in leaves, which are linked in
a sorted chain). The planner uses it for equality and range predicates on indexed
columns instead of scanning the whole heap.

Structure:
  * Leaf node  : sorted `keys`, parallel `values` (RIDs), `next` -> right sibling.
  * Inner node : sorted routing `keys`, `children` (len = len(keys) + 1).

Rebalancing:
  * insert -> split an overfull node, pushing a separator key up (copied up for
    leaves, moved up for inner nodes); a root split grows the tree by one level.
  * delete -> on underflow, borrow a key from a sibling, else merge with a
    sibling and pull the separator down; the root can shrink by one level.

Design note: this index is intentionally in-memory and rebuilt from the base
heap when a table is opened (see catalog.rebuild_indexes). Durability is provided
solely by the heap + WAL, so an index can never be left inconsistent by a crash.
"""

from __future__ import annotations

from typing import Any, Iterator

from .heap import RID

DEFAULT_ORDER = 64  # max keys per node (fanout = order + 1 for inner nodes)


class _Leaf:
    __slots__ = ("keys", "values", "next")

    def __init__(self) -> None:
        self.keys: list[Any] = []
        self.values: list[RID] = []
        self.next: "_Leaf | None" = None

    leaf = True


class _Inner:
    __slots__ = ("keys", "children")

    def __init__(self) -> None:
        self.keys: list[Any] = []
        self.children: list[Any] = []  # list of _Leaf | _Inner

    leaf = False


class DuplicateKeyError(KeyError):
    """Raised when inserting a key that already exists in a unique index."""


class BPlusTree:
    def __init__(self, order: int = DEFAULT_ORDER, unique: bool = True) -> None:
        if order < 3:
            raise ValueError("B+ tree order must be >= 3")
        self.order = order            # max keys per node
        self.min_keys = order // 2    # min keys per non-root node
        self.unique = unique
        self.root: Any = _Leaf()
        self.size = 0

    # --- search ------------------------------------------------------------

    def _find_leaf(self, key: Any) -> _Leaf:
        node = self.root
        while not node.leaf:
            i = self._upper_child(node, key)
            node = node.children[i]
        return node

    @staticmethod
    def _upper_child(node: _Inner, key: Any) -> int:
        # choose child index: first key > search key
        i = 0
        while i < len(node.keys) and key >= node.keys[i]:
            i += 1
        return i

    def search(self, key: Any) -> RID | None:
        """Return the RID for `key`, or None if absent."""
        leaf = self._find_leaf(key)
        for k, v in zip(leaf.keys, leaf.values):
            if k == key:
                return v
        return None

    def __contains__(self, key: Any) -> bool:
        return self.search(key) is not None

    def range(self, low: Any = None, high: Any = None) -> Iterator[tuple[Any, RID]]:
        """Yield (key, RID) for low <= key <= high in sorted order.

        None bounds mean unbounded. Walks the linked leaf chain.
        """
        leaf = self._find_leaf(low) if low is not None else self._leftmost_leaf()
        while leaf is not None:
            for k, v in zip(leaf.keys, leaf.values):
                if low is not None and k < low:
                    continue
                if high is not None and k > high:
                    return
                yield k, v
            leaf = leaf.next

    def items(self) -> Iterator[tuple[Any, RID]]:
        """All (key, RID) pairs in sorted key order."""
        return self.range()

    def _leftmost_leaf(self) -> _Leaf:
        node = self.root
        while not node.leaf:
            node = node.children[0]
        return node

    # --- insert ------------------------------------------------------------

    def insert(self, key: Any, value: RID) -> None:
        split = self._insert(self.root, key, value)
        if split is not None:
            sep, right = split
            new_root = _Inner()
            new_root.keys = [sep]
            new_root.children = [self.root, right]
            self.root = new_root

    def _insert(self, node: Any, key: Any, value: RID):
        if node.leaf:
            return self._insert_leaf(node, key, value)
        i = self._upper_child(node, key)
        split = self._insert(node.children[i], key, value)
        if split is None:
            return None
        sep, right = split
        node.keys.insert(i, sep)
        node.children.insert(i + 1, right)
        if len(node.keys) <= self.order:
            return None
        return self._split_inner(node)

    def _insert_leaf(self, leaf: _Leaf, key: Any, value: RID):
        # locate position
        i = 0
        while i < len(leaf.keys) and leaf.keys[i] < key:
            i += 1
        if i < len(leaf.keys) and leaf.keys[i] == key:
            if self.unique:
                raise DuplicateKeyError(f"duplicate key: {key!r}")
            leaf.values[i] = value  # non-unique: overwrite
            return None
        leaf.keys.insert(i, key)
        leaf.values.insert(i, value)
        self.size += 1
        if len(leaf.keys) <= self.order:
            return None
        return self._split_leaf(leaf)

    def _split_leaf(self, leaf: _Leaf):
        mid = len(leaf.keys) // 2
        right = _Leaf()
        right.keys = leaf.keys[mid:]
        right.values = leaf.values[mid:]
        leaf.keys = leaf.keys[:mid]
        leaf.values = leaf.values[:mid]
        right.next = leaf.next
        leaf.next = right
        return right.keys[0], right  # separator is COPIED up

    def _split_inner(self, node: _Inner):
        mid = len(node.keys) // 2
        sep = node.keys[mid]  # separator MOVES up (removed from this level)
        right = _Inner()
        right.keys = node.keys[mid + 1 :]
        right.children = node.children[mid + 1 :]
        node.keys = node.keys[:mid]
        node.children = node.children[: mid + 1]
        return sep, right

    # --- delete ------------------------------------------------------------

    def delete(self, key: Any) -> bool:
        """Remove `key`. Returns True if it was present."""
        found = self._delete(self.root, key)
        # shrink root if it became an empty inner node
        if not self.root.leaf and len(self.root.keys) == 0:
            self.root = self.root.children[0]
        return found

    def _delete(self, node: Any, key: Any) -> bool:
        if node.leaf:
            if key in node.keys:
                idx = node.keys.index(key)
                node.keys.pop(idx)
                node.values.pop(idx)
                self.size -= 1
                return True
            return False
        i = self._upper_child(node, key)
        child = node.children[i]
        found = self._delete(child, key)
        if found and len(child.keys) < self.min_keys:
            self._rebalance(node, i)
        return found

    def _rebalance(self, parent: _Inner, i: int) -> None:
        """Fix underflow of parent.children[i] via borrow or merge."""
        child = parent.children[i]
        left = parent.children[i - 1] if i > 0 else None
        right = parent.children[i + 1] if i + 1 < len(parent.children) else None

        # 1) borrow from a sibling with spare keys
        if left is not None and len(left.keys) > self.min_keys:
            self._borrow_from_left(parent, i, child, left)
            return
        if right is not None and len(right.keys) > self.min_keys:
            self._borrow_from_right(parent, i, child, right)
            return
        # 2) otherwise merge with a sibling
        if left is not None:
            self._merge(parent, i - 1)  # merge child into left
        else:
            self._merge(parent, i)      # merge right into child

    def _borrow_from_left(self, parent, i, child, left) -> None:
        if child.leaf:
            child.keys.insert(0, left.keys.pop())
            child.values.insert(0, left.values.pop())
            parent.keys[i - 1] = child.keys[0]
        else:
            child.keys.insert(0, parent.keys[i - 1])
            parent.keys[i - 1] = left.keys.pop()
            child.children.insert(0, left.children.pop())

    def _borrow_from_right(self, parent, i, child, right) -> None:
        if child.leaf:
            child.keys.append(right.keys.pop(0))
            child.values.append(right.values.pop(0))
            parent.keys[i] = right.keys[0]
        else:
            child.keys.append(parent.keys[i])
            parent.keys[i] = right.keys.pop(0)
            child.children.append(right.children.pop(0))

    def _merge(self, parent: _Inner, i: int) -> None:
        """Merge parent.children[i+1] into parent.children[i]."""
        left = parent.children[i]
        right = parent.children[i + 1]
        if left.leaf:
            left.keys += right.keys
            left.values += right.values
            left.next = right.next
        else:
            left.keys.append(parent.keys[i])      # pull separator down
            left.keys += right.keys
            left.children += right.children
        parent.keys.pop(i)
        parent.children.pop(i + 1)

    # --- introspection (used by tests + the optimizer's stats) -------------

    def __len__(self) -> int:
        return self.size

    def height(self) -> int:
        h, node = 1, self.root
        while not node.leaf:
            h += 1
            node = node.children[0]
        return h
