"""
B+ Tree implementation.
- Internal nodes hold keys and child pointers.
- Leaf nodes hold keys, values (page_id, slot_id), and a next-leaf pointer.
- ORDER = max number of keys per node. Split when full.
"""
import json
import os

ORDER = 64  # max keys per node


class LeafNode:
    def __init__(self):
        self.keys = []
        self.values = []   # list of (page_id, slot_id)
        self.next = None   # pointer to next leaf (for range scans)

    def is_full(self):
        return len(self.keys) >= ORDER

    def insert(self, key, value):
        i = self._find_pos(key)
        if i < len(self.keys) and self.keys[i] == key:
            self.values[i] = value  # update
        else:
            self.keys.insert(i, key)
            self.values.insert(i, value)

    def delete(self, key) -> bool:
        i = self._find_pos(key)
        if i < len(self.keys) and self.keys[i] == key:
            self.keys.pop(i)
            self.values.pop(i)
            return True
        return False

    def search(self, key):
        i = self._find_pos(key)
        if i < len(self.keys) and self.keys[i] == key:
            return self.values[i]
        return None

    def _find_pos(self, key) -> int:
        lo, hi = 0, len(self.keys)
        while lo < hi:
            mid = (lo + hi) // 2
            if self.keys[mid] < key:
                lo = mid + 1
            else:
                hi = mid
        return lo

    def split(self):
        """Split this leaf in half. Return (new_right_leaf, promoted_key)."""
        mid = len(self.keys) // 2
        right = LeafNode()
        right.keys = self.keys[mid:]
        right.values = self.values[mid:]
        right.next = self.next
        self.keys = self.keys[:mid]
        self.values = self.values[:mid]
        self.next = right
        return right, right.keys[0]  # promote first key of right leaf


class InternalNode:
    def __init__(self):
        self.keys = []      # len = len(children) - 1
        self.children = []  # len = len(keys) + 1

    def is_full(self):
        return len(self.keys) >= ORDER

    def find_child(self, key) -> int:
        """Return index of child subtree for key."""
        lo, hi = 0, len(self.keys)
        while lo < hi:
            mid = (lo + hi) // 2
            if self.keys[mid] <= key:
                lo = mid + 1
            else:
                hi = mid
        return lo

    def insert_child(self, key, right_child):
        """Insert promoted key and right child after a split."""
        i = self.find_child(key)
        self.keys.insert(i, key)
        self.children.insert(i + 1, right_child)

    def split(self):
        """Split internal node. Middle key is promoted up."""
        mid = len(self.keys) // 2
        promoted_key = self.keys[mid]
        right = InternalNode()
        right.keys = self.keys[mid + 1:]
        right.children = self.children[mid + 1:]
        self.keys = self.keys[:mid]
        self.children = self.children[:mid + 1]
        return right, promoted_key


class BPlusTree:
    def __init__(self, name: str = 'index'):
        self.name = name
        self.root = LeafNode()

    # ── public API ────────────────────────────────────────────────────────────

    def insert(self, key, value):
        result = self._insert(self.root, key, value)
        if result:
            # root was split
            right_child, promoted_key = result
            new_root = InternalNode()
            new_root.keys = [promoted_key]
            new_root.children = [self.root, right_child]
            self.root = new_root

    def search(self, key):
        leaf = self._find_leaf(key)
        return leaf.search(key)

    def delete(self, key) -> bool:
        return self._delete(self.root, key, None, -1)

    def range_scan(self, low, high):
        """Yield (key, value) pairs where low <= key <= high."""
        leaf = self._find_leaf(low)
        while leaf is not None:
            for k, v in zip(leaf.keys, leaf.values):
                if k > high:
                    return
                if k >= low:
                    yield k, v
            leaf = leaf.next

    def all_entries(self):
        """Yield all (key, value) pairs in order."""
        leaf = self._leftmost_leaf()
        while leaf is not None:
            yield from zip(leaf.keys, leaf.values)
            leaf = leaf.next

    # ── internal helpers ─────────────────────────────────────────────────────

    def _find_leaf(self, key) -> LeafNode:
        node = self.root
        while isinstance(node, InternalNode):
            node = node.children[node.find_child(key)]
        return node

    def _leftmost_leaf(self) -> LeafNode:
        node = self.root
        while isinstance(node, InternalNode):
            node = node.children[0]
        return node

    def _insert(self, node, key, value):
        """Returns (right_sibling, promoted_key) if split occurred, else None."""
        if isinstance(node, LeafNode):
            node.insert(key, value)
            if node.is_full():
                return node.split()
            return None

        # internal node
        i = node.find_child(key)
        result = self._insert(node.children[i], key, value)
        if result:
            right_child, promoted_key = result
            node.insert_child(promoted_key, right_child)
            if node.is_full():
                return node.split()
        return None

    def _delete(self, node, key, parent, child_index) -> bool:
        if isinstance(node, LeafNode):
            return node.delete(key)
        i = node.find_child(key)
        return self._delete(node.children[i], key, node, i)

    # ── persistence ───────────────────────────────────────────────────────────

    def save(self, path: str):
        """Serialize tree to JSON file."""
        data = self._serialize_node(self.root)
        with open(path, 'w') as f:
            json.dump(data, f)

    def load(self, path: str):
        if not os.path.exists(path):
            return
        with open(path, 'r') as f:
            data = json.load(f)
        self.root = self._deserialize_node(data)

    def _serialize_node(self, node) -> dict:
        if isinstance(node, LeafNode):
            return {
                'type': 'leaf',
                'keys': node.keys,
                'values': node.values,
                'next': self._serialize_node(node.next) if node.next else None,
            }
        return {
            'type': 'internal',
            'keys': node.keys,
            'children': [self._serialize_node(c) for c in node.children],
        }

    def _deserialize_node(self, data: dict):
        if data['type'] == 'leaf':
            node = LeafNode()
            node.keys = data['keys']
            node.values = [tuple(v) for v in data['values']]
            node.next = self._deserialize_node(data['next']) if data['next'] else None
            return node
        node = InternalNode()
        node.keys = data['keys']
        node.children = [self._deserialize_node(c) for c in data['children']]
        return node
