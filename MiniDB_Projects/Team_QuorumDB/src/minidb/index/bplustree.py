"""B+Tree index mapping ordered keys to record identifiers (RIDs).

This is a textbook in-memory B+Tree:

* **Leaf nodes** hold sorted keys, each mapped to a list of RIDs, and are
  chained left-to-right (``next``) so range scans are a single linked-list
  walk after one root-to-leaf descent.
* **Internal nodes** hold separator keys and child pointers; a node with
  *k* keys has *k+1* children.
* ``order`` is the fan-out (max children of an internal node). Overflowing
  nodes **split**; underflowing nodes **borrow** from a sibling or **merge**,
  keeping the tree balanced with height O(log n).

The tree supports both **unique** indexes (primary key — duplicate keys are
rejected) and **non-unique** indexes (secondary — a key maps to several RIDs).

MiniDB rebuilds indexes from the recovered base table on startup, so the tree
itself is not persisted or logged; recovery concerns only the heap data. This
keeps index code free of WAL coupling and is documented as a design trade-off.
"""

from __future__ import annotations

from typing import Any, Iterator, List, Optional, Tuple

from ..storage.rid import RID


class _Node:
    __slots__ = ("is_leaf", "keys", "children", "values", "next")

    def __init__(self, is_leaf: bool):
        self.is_leaf = is_leaf
        self.keys: List[Any] = []
        self.children: List["_Node"] = []          # internal: k+1 child pointers
        self.values: List[List[RID]] = []           # leaf: one RID-list per key
        self.next: Optional["_Node"] = None         # leaf sibling chain


class DuplicateKeyError(Exception):
    """Raised when inserting a duplicate key into a unique index."""


class BPlusTree:
    def __init__(self, order: int = 64, unique: bool = False):
        if order < 3:
            raise ValueError("order (fan-out) must be >= 3")
        self.order = order
        self.unique = unique
        self.max_keys = order - 1
        self.min_keys = (order - 1) // 2 or 1
        self.root = _Node(is_leaf=True)
        self._size = 0  # number of (key, rid) entries

    def __len__(self) -> int:
        return self._size

    # -- search -------------------------------------------------------------
    def _find_leaf(self, key: Any) -> _Node:
        node = self.root
        while not node.is_leaf:
            i = self._upper_child(node.keys, key)
            node = node.children[i]
        return node

    @staticmethod
    def _upper_child(keys: List[Any], key: Any) -> int:
        # Index of the child to descend into: first separator > key.
        lo, hi = 0, len(keys)
        while lo < hi:
            mid = (lo + hi) // 2
            if key < keys[mid]:
                hi = mid
            else:
                lo = mid + 1
        return lo

    @staticmethod
    def _leaf_pos(keys: List[Any], key: Any) -> int:
        lo, hi = 0, len(keys)
        while lo < hi:
            mid = (lo + hi) // 2
            if keys[mid] < key:
                lo = mid + 1
            else:
                hi = mid
        return lo

    def search(self, key: Any) -> List[RID]:
        """Return the RID list for *key* (empty if absent)."""
        leaf = self._find_leaf(key)
        i = self._leaf_pos(leaf.keys, key)
        if i < len(leaf.keys) and leaf.keys[i] == key:
            return list(leaf.values[i])
        return []

    def contains(self, key: Any) -> bool:
        leaf = self._find_leaf(key)
        i = self._leaf_pos(leaf.keys, key)
        return i < len(leaf.keys) and leaf.keys[i] == key

    def range(self, low: Any = None, high: Any = None,
              include_low: bool = True, include_high: bool = True
              ) -> Iterator[Tuple[Any, RID]]:
        """Yield ``(key, rid)`` for keys in [low, high] in ascending order.

        ``None`` bounds mean unbounded. This is the operation an index scan
        uses to satisfy range predicates without touching the heap.
        """
        node = self.root if low is None else self._find_leaf(low)
        while not node.is_leaf:  # low is None -> walk to leftmost leaf
            node = node.children[0]
        started = low is None
        while node is not None:
            for k, rids in zip(node.keys, node.values):
                if not started:
                    if k < low or (not include_low and k == low):
                        continue
                    started = True
                if high is not None and (k > high or (not include_high and k == high)):
                    return
                for rid in rids:
                    yield k, rid
            node = node.next

    # -- insert -------------------------------------------------------------
    def insert(self, key: Any, rid: RID) -> None:
        split = self._insert(self.root, key, rid)
        if split is not None:
            sep, right = split
            new_root = _Node(is_leaf=False)
            new_root.keys = [sep]
            new_root.children = [self.root, right]
            self.root = new_root

    def _insert(self, node: _Node, key: Any, rid: RID
                ) -> Optional[Tuple[Any, _Node]]:
        if node.is_leaf:
            i = self._leaf_pos(node.keys, key)
            if i < len(node.keys) and node.keys[i] == key:
                if self.unique:
                    raise DuplicateKeyError(f"duplicate key {key!r}")
                node.values[i].append(rid)
                self._size += 1
                return None
            node.keys.insert(i, key)
            node.values.insert(i, [rid])
            self._size += 1
            if len(node.keys) > self.max_keys:
                return self._split_leaf(node)
            return None

        i = self._upper_child(node.keys, key)
        split = self._insert(node.children[i], key, rid)
        if split is None:
            return None
        sep, right = split
        node.keys.insert(i, sep)
        node.children.insert(i + 1, right)
        if len(node.keys) > self.max_keys:
            return self._split_internal(node)
        return None

    def _split_leaf(self, node: _Node) -> Tuple[Any, _Node]:
        mid = (len(node.keys) + 1) // 2
        right = _Node(is_leaf=True)
        right.keys = node.keys[mid:]
        right.values = node.values[mid:]
        node.keys = node.keys[:mid]
        node.values = node.values[:mid]
        right.next = node.next
        node.next = right
        return right.keys[0], right  # separator is a copy of right's first key

    def _split_internal(self, node: _Node) -> Tuple[Any, _Node]:
        mid = len(node.keys) // 2
        sep = node.keys[mid]               # middle key moves up (not copied)
        right = _Node(is_leaf=False)
        right.keys = node.keys[mid + 1:]
        right.children = node.children[mid + 1:]
        node.keys = node.keys[:mid]
        node.children = node.children[:mid + 1]
        return sep, right

    # -- delete -------------------------------------------------------------
    def delete(self, key: Any, rid: Optional[RID] = None) -> bool:
        """Remove ``(key, rid)``; if *rid* is None remove every RID for *key*.

        Returns True if anything was removed.
        """
        removed = self._delete(self.root, key, rid)
        # Collapse the root if it became an empty internal node.
        if not self.root.is_leaf and len(self.root.keys) == 0:
            self.root = self.root.children[0]
        return removed

    def _delete(self, node: _Node, key: Any, rid: Optional[RID]) -> bool:
        if node.is_leaf:
            i = self._leaf_pos(node.keys, key)
            if i >= len(node.keys) or node.keys[i] != key:
                return False
            if rid is None:
                self._size -= len(node.values[i])
                node.keys.pop(i)
                node.values.pop(i)
            else:
                try:
                    node.values[i].remove(rid)
                    self._size -= 1
                except ValueError:
                    return False
                if not node.values[i]:
                    node.keys.pop(i)
                    node.values.pop(i)
            return True

        i = self._upper_child(node.keys, key)
        removed = self._delete(node.children[i], key, rid)
        if removed and len(node.children[i].keys) < self.min_keys:
            self._rebalance(node, i)
        return removed

    def _rebalance(self, parent: _Node, idx: int) -> None:
        child = parent.children[idx]
        # Try to borrow from the left sibling.
        if idx > 0 and len(parent.children[idx - 1].keys) > self.min_keys:
            self._borrow_from_left(parent, idx)
        # Then the right sibling.
        elif idx < len(parent.children) - 1 and \
                len(parent.children[idx + 1].keys) > self.min_keys:
            self._borrow_from_right(parent, idx)
        # Otherwise merge with a sibling.
        elif idx > 0:
            self._merge(parent, idx - 1)
        else:
            self._merge(parent, idx)

    def _borrow_from_left(self, parent: _Node, idx: int) -> None:
        child, left = parent.children[idx], parent.children[idx - 1]
        if child.is_leaf:
            child.keys.insert(0, left.keys.pop())
            child.values.insert(0, left.values.pop())
            parent.keys[idx - 1] = child.keys[0]
        else:
            child.keys.insert(0, parent.keys[idx - 1])
            parent.keys[idx - 1] = left.keys.pop()
            child.children.insert(0, left.children.pop())

    def _borrow_from_right(self, parent: _Node, idx: int) -> None:
        child, right = parent.children[idx], parent.children[idx + 1]
        if child.is_leaf:
            child.keys.append(right.keys.pop(0))
            child.values.append(right.values.pop(0))
            parent.keys[idx] = right.keys[0]
        else:
            child.keys.append(parent.keys[idx])
            parent.keys[idx] = right.keys.pop(0)
            child.children.append(right.children.pop(0))

    def _merge(self, parent: _Node, left_idx: int) -> None:
        left = parent.children[left_idx]
        right = parent.children[left_idx + 1]
        if left.is_leaf:
            left.keys.extend(right.keys)
            left.values.extend(right.values)
            left.next = right.next
        else:
            left.keys.append(parent.keys[left_idx])
            left.keys.extend(right.keys)
            left.children.extend(right.children)
        parent.keys.pop(left_idx)
        parent.children.pop(left_idx + 1)

    # -- diagnostics --------------------------------------------------------
    def height(self) -> int:
        h, node = 1, self.root
        while not node.is_leaf:
            h += 1
            node = node.children[0]
        return h

    def items(self) -> Iterator[Tuple[Any, RID]]:
        yield from self.range()
