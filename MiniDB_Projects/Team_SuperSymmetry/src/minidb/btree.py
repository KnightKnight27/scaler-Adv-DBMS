"""
B+ Tree index.

Maps index keys -> lists of RIDs (a list, so the same structure serves both
unique primary-key indexes and non-unique secondary indexes). Internal nodes
route searches; leaf nodes hold the data entries and are linked left-to-right
to support efficient range scans.

Supported operations: search (point lookup), range_scan, insert, delete
(with sibling borrow and node merge to keep the tree balanced).

This implementation keeps nodes as in-memory objects and is persisted by
pickling (see Database). A fully page-backed B+ tree is noted as a possible
extension in the README; an in-memory tree was chosen for clarity and
provable correctness during the viva.
"""
from __future__ import annotations

import bisect
from typing import Any, List, Optional, Tuple

RID = Tuple[int, int]


class _Node:
    __slots__ = ("leaf", "keys", "children", "rids", "next")

    def __init__(self, leaf: bool):
        self.leaf = leaf
        self.keys: List[Any] = []
        self.children: List["_Node"] = []      # internal nodes
        self.rids: List[List[RID]] = []         # leaf nodes: parallel to keys
        self.next: Optional["_Node"] = None     # leaf chain


class BPlusTree:
    def __init__(self, order: int = 64):
        # order = max number of keys in a node before it splits
        self.order = order
        self.root = _Node(leaf=True)
        self.height = 1

    # ---- search ----------------------------------------------------------
    def _find_leaf(self, key: Any) -> _Node:
        node = self.root
        while not node.leaf:
            i = bisect.bisect_right(node.keys, key)
            node = node.children[i]
        return node

    def search(self, key: Any) -> List[RID]:
        leaf = self._find_leaf(key)
        i = bisect.bisect_left(leaf.keys, key)
        if i < len(leaf.keys) and leaf.keys[i] == key:
            return list(leaf.rids[i])
        return []

    def range_scan(self, low: Any = None, high: Any = None) -> List[RID]:
        """Inclusive range scan. None means unbounded on that side."""
        if low is None:
            node = self._leftmost_leaf()
            i = 0
        else:
            node = self._find_leaf(low)
            i = bisect.bisect_left(node.keys, low)
        out: List[RID] = []
        while node is not None:
            while i < len(node.keys):
                k = node.keys[i]
                if high is not None and k > high:
                    return out
                out.extend(node.rids[i])
                i += 1
            node = node.next
            i = 0
        return out

    def _leftmost_leaf(self) -> _Node:
        node = self.root
        while not node.leaf:
            node = node.children[0]
        return node

    # ---- insert ----------------------------------------------------------
    def insert(self, key: Any, rid: RID):
        result = self._insert(self.root, key, rid)
        if result is not None:
            sep, right = result
            new_root = _Node(leaf=False)
            new_root.keys = [sep]
            new_root.children = [self.root, right]
            self.root = new_root
            self.height += 1

    def _insert(self, node: _Node, key: Any, rid: RID):
        if node.leaf:
            i = bisect.bisect_left(node.keys, key)
            if i < len(node.keys) and node.keys[i] == key:
                node.rids[i].append(rid)  # duplicate key
                return None
            node.keys.insert(i, key)
            node.rids.insert(i, [rid])
            if len(node.keys) > self.order:
                return self._split_leaf(node)
            return None
        else:
            i = bisect.bisect_right(node.keys, key)
            res = self._insert(node.children[i], key, rid)
            if res is None:
                return None
            sep, right = res
            node.keys.insert(i, sep)
            node.children.insert(i + 1, right)
            if len(node.keys) > self.order:
                return self._split_internal(node)
            return None

    def _split_leaf(self, node: _Node):
        mid = len(node.keys) // 2
        right = _Node(leaf=True)
        right.keys = node.keys[mid:]
        right.rids = node.rids[mid:]
        node.keys = node.keys[:mid]
        node.rids = node.rids[:mid]
        right.next = node.next
        node.next = right
        return right.keys[0], right  # copy-up separator

    def _split_internal(self, node: _Node):
        mid = len(node.keys) // 2
        sep = node.keys[mid]
        right = _Node(leaf=False)
        right.keys = node.keys[mid + 1 :]
        right.children = node.children[mid + 1 :]
        node.keys = node.keys[:mid]
        node.children = node.children[: mid + 1]
        return sep, right  # push-up separator

    # ---- delete ----------------------------------------------------------
    def delete(self, key: Any, rid: Optional[RID] = None) -> bool:
        ok = self._delete(self.root, key, rid)
        # collapse root if it became an internal node with a single child
        if not self.root.leaf and len(self.root.keys) == 0:
            self.root = self.root.children[0]
            self.height -= 1
        return ok

    def _min_keys(self) -> int:
        return self.order // 2

    def _delete(self, node: _Node, key: Any, rid: Optional[RID]) -> bool:
        if node.leaf:
            i = bisect.bisect_left(node.keys, key)
            if i >= len(node.keys) or node.keys[i] != key:
                return False
            if rid is not None and rid in node.rids[i]:
                node.rids[i].remove(rid)
                if node.rids[i]:
                    return True  # other RIDs remain for this key
            # remove the whole key entry
            node.keys.pop(i)
            node.rids.pop(i)
            return True
        else:
            i = bisect.bisect_right(node.keys, key)
            child = node.children[i]
            ok = self._delete(child, key, rid)
            if not ok:
                return False
            if len(child.keys) < self._min_keys():
                self._rebalance(node, i)
            return True

    def _rebalance(self, parent: _Node, idx: int):
        child = parent.children[idx]
        # try borrow from left sibling
        if idx > 0 and len(parent.children[idx - 1].keys) > self._min_keys():
            left = parent.children[idx - 1]
            if child.leaf:
                child.keys.insert(0, left.keys.pop())
                child.rids.insert(0, left.rids.pop())
                parent.keys[idx - 1] = child.keys[0]
            else:
                child.keys.insert(0, parent.keys[idx - 1])
                parent.keys[idx - 1] = left.keys.pop()
                child.children.insert(0, left.children.pop())
            return
        # try borrow from right sibling
        if idx < len(parent.children) - 1 and len(
            parent.children[idx + 1].keys
        ) > self._min_keys():
            right = parent.children[idx + 1]
            if child.leaf:
                child.keys.append(right.keys.pop(0))
                child.rids.append(right.rids.pop(0))
                parent.keys[idx] = right.keys[0]
            else:
                child.keys.append(parent.keys[idx])
                parent.keys[idx] = right.keys.pop(0)
                child.children.append(right.children.pop(0))
            return
        # otherwise merge with a sibling
        if idx > 0:
            self._merge(parent, idx - 1)
        else:
            self._merge(parent, idx)

    def _merge(self, parent: _Node, left_idx: int):
        left = parent.children[left_idx]
        right = parent.children[left_idx + 1]
        if left.leaf:
            left.keys.extend(right.keys)
            left.rids.extend(right.rids)
            left.next = right.next
        else:
            left.keys.append(parent.keys[left_idx])
            left.keys.extend(right.keys)
            left.children.extend(right.children)
        parent.keys.pop(left_idx)
        parent.children.pop(left_idx + 1)

    # ---- introspection ---------------------------------------------------
    def items(self):
        node = self._leftmost_leaf()
        while node is not None:
            for k, rids in zip(node.keys, node.rids):
                yield k, list(rids)
            node = node.next
