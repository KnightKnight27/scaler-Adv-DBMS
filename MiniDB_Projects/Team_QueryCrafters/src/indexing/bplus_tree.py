import os
import json
import time

class BPlusNode:
    def __init__(self, is_leaf: bool = False):
        self.is_leaf = is_leaf
        self.keys = []
        # If is_leaf is True, children contains values: (page_id, slot_id)
        # If is_leaf is False, children contains BPlusNode instances
        self.children = []
        self.next = None
        self.prev = None

class BPlusTree:
    def __init__(self, idx_file_path: str = None, order: int = 4):
        self.idx_file_path = idx_file_path
        self.order = order
        self.root = BPlusNode(is_leaf=True)
        self.leaf_head = self.root
        self.last_save_time = 0.0
        if idx_file_path and os.path.exists(idx_file_path):
            self.load()

    def search(self, key):
        node = self.root
        while not node.is_leaf:
            idx = 0
            while idx < len(node.keys) and key >= node.keys[idx]:
                idx += 1
            node = node.children[idx]
        
        for i, k in enumerate(node.keys):
            if k == key:
                return node.children[i]
        return None

    def range_scan(self, low, high):
        results = []
        
        # Find start leaf
        node = self.root
        while not node.is_leaf:
            idx = 0
            if low is not None:
                while idx < len(node.keys) and low >= node.keys[idx]:
                    idx += 1
            node = node.children[idx]

        # Scan forward
        curr = node
        while curr:
            for i, k in enumerate(curr.keys):
                if low is not None and k < low:
                    continue
                if high is not None and k > high:
                    return results
                results.append(curr.children[i])
            curr = curr.next
        return results

    def insert(self, key, value):
        # Find leaf node
        node = self.root
        path = []
        while not node.is_leaf:
            path.append(node)
            idx = 0
            while idx < len(node.keys) and key >= node.keys[idx]:
                idx += 1
            node = node.children[idx]

        # Insert into leaf
        idx = 0
        while idx < len(node.keys) and key > node.keys[idx]:
            idx += 1
        
        if idx < len(node.keys) and node.keys[idx] == key:
            # Duplicate key: overwrite or handle
            node.children[idx] = value
        else:
            node.keys.insert(idx, key)
            node.children.insert(idx, value)

        # Check split
        if len(node.keys) > self.order:
            self._split_leaf(node, path)
        
        self.save()

    def _split_leaf(self, leaf: BPlusNode, path: list):
        # Split leaf into two
        new_leaf = BPlusNode(is_leaf=True)
        mid = len(leaf.keys) // 2  # e.g., 5 // 2 = 2
        
        new_leaf.keys = leaf.keys[mid:]
        new_leaf.children = leaf.children[mid:]
        
        leaf.keys = leaf.keys[:mid]
        leaf.children = leaf.children[:mid]

        # Update sibling pointers
        new_leaf.next = leaf.next
        new_leaf.prev = leaf
        if leaf.next:
            leaf.next.prev = new_leaf
        leaf.next = new_leaf

        promote_key = new_leaf.keys[0]

        if not path:
            # Create new root
            new_root = BPlusNode(is_leaf=False)
            new_root.keys = [promote_key]
            new_root.children = [leaf, new_leaf]
            self.root = new_root
        else:
            parent = path.pop()
            self._insert_internal(parent, promote_key, new_leaf, path)

    def _insert_internal(self, parent: BPlusNode, key, child: BPlusNode, path: list):
        idx = 0
        while idx < len(parent.keys) and key > parent.keys[idx]:
            idx += 1
        parent.keys.insert(idx, key)
        parent.children.insert(idx + 1, child)

        if len(parent.keys) > self.order:
            self._split_internal(parent, path)

    def _split_internal(self, node: BPlusNode, path: list):
        new_node = BPlusNode(is_leaf=False)
        mid = len(node.keys) // 2  # 5 // 2 = 2
        
        promote_key = node.keys[mid]
        
        new_node.keys = node.keys[mid + 1:]
        new_node.children = node.children[mid + 1:]
        
        node.keys = node.keys[:mid]
        node.children = node.children[:mid + 1]

        if not path:
            new_root = BPlusNode(is_leaf=False)
            new_root.keys = [promote_key]
            new_root.children = [node, new_node]
            self.root = new_root
        else:
            parent = path.pop()
            self._insert_internal(parent, promote_key, new_node, path)

    def delete(self, key):
        path = []
        node = self.root
        while not node.is_leaf:
            path.append(node)
            idx = 0
            while idx < len(node.keys) and key >= node.keys[idx]:
                idx += 1
            node = node.children[idx]

        # Find key in leaf
        idx = -1
        for i, k in enumerate(node.keys):
            if k == key:
                idx = i
                break
        
        if idx == -1:
            return False  # Key not found

        node.keys.pop(idx)
        node.children.pop(idx)

        # Handle root underflow
        if node == self.root:
            self.save()
            return True

        # Check underflow (minimum 2 keys for leaf)
        if len(node.keys) < 2:
            self._handle_leaf_underflow(node, path)

        self.save()
        return True

    def _handle_leaf_underflow(self, leaf: BPlusNode, path: list):
        parent = path[-1]
        leaf_idx = parent.children.index(leaf)

        # Try to borrow from left sibling
        if leaf_idx > 0:
            left_sib = parent.children[leaf_idx - 1]
            if len(left_sib.keys) > 2:
                # Borrow last element from left sibling
                borrowed_key = left_sib.keys.pop()
                borrowed_val = left_sib.children.pop()
                leaf.keys.insert(0, borrowed_key)
                leaf.children.insert(0, borrowed_val)
                parent.keys[leaf_idx - 1] = leaf.keys[0]
                return

        # Try to borrow from right sibling
        if leaf_idx < len(parent.children) - 1:
            right_sib = parent.children[leaf_idx + 1]
            if len(right_sib.keys) > 2:
                # Borrow first element from right sibling
                borrowed_key = right_sib.keys.pop(0)
                borrowed_val = right_sib.children.pop(0)
                leaf.keys.append(borrowed_key)
                leaf.children.append(borrowed_val)
                parent.keys[leaf_idx] = right_sib.keys[0]
                return

        # Merge with left sibling
        if leaf_idx > 0:
            left_sib = parent.children[leaf_idx - 1]
            # Merge leaf into left sibling
            left_sib.keys.extend(leaf.keys)
            left_sib.children.extend(leaf.children)
            # Update doubly-linked list pointers
            left_sib.next = leaf.next
            if leaf.next:
                leaf.next.prev = left_sib
            
            # Remove separating key and pointer from parent
            parent.keys.pop(leaf_idx - 1)
            parent.children.pop(leaf_idx)
            
            if len(parent.keys) < 1:
                self._handle_internal_underflow(parent, path[:-1])
            return

        # Merge with right sibling
        if leaf_idx < len(parent.children) - 1:
            right_sib = parent.children[leaf_idx + 1]
            # Merge right sibling into leaf
            leaf.keys.extend(right_sib.keys)
            leaf.children.extend(right_sib.children)
            leaf.next = right_sib.next
            if right_sib.next:
                right_sib.next.prev = leaf
            
            # Remove separating key and pointer from parent
            parent.keys.pop(leaf_idx)
            parent.children.pop(leaf_idx + 1)
            
            if len(parent.keys) < 1:
                self._handle_internal_underflow(parent, path[:-1])
            return

    def _handle_internal_underflow(self, node: BPlusNode, path: list):
        if node == self.root:
            if len(node.keys) == 0 and len(node.children) > 0:
                self.root = node.children[0]
            return

        parent = path[-1]
        node_idx = parent.children.index(node)

        # Try to borrow from left sibling
        if node_idx > 0:
            left_sib = parent.children[node_idx - 1]
            if len(left_sib.keys) > 1:
                # Borrow from left sibling
                parent_key = parent.keys[node_idx - 1]
                # Sibling's last key goes to parent; parent key goes to node; sibling's last child goes to node
                node.keys.insert(0, parent_key)
                node.children.insert(0, left_sib.children.pop())
                parent.keys[node_idx - 1] = left_sib.keys.pop()
                return

        # Try to borrow from right sibling
        if node_idx < len(parent.children) - 1:
            right_sib = parent.children[node_idx + 1]
            if len(right_sib.keys) > 1:
                # Borrow from right sibling
                parent_key = parent.keys[node_idx]
                # Sibling's first key goes to parent; parent key goes to node; sibling's first child goes to node
                node.keys.append(parent_key)
                node.children.append(right_sib.children.pop(0))
                parent.keys[node_idx] = right_sib.keys.pop(0)
                return

        # Merge with left sibling
        if node_idx > 0:
            left_sib = parent.children[node_idx - 1]
            parent_key = parent.keys.pop(node_idx - 1)
            # Merge parent key and node's keys/children into left sibling
            left_sib.keys.append(parent_key)
            left_sib.keys.extend(node.keys)
            left_sib.children.extend(node.children)
            parent.children.pop(node_idx)

            if len(parent.keys) < 1:
                self._handle_internal_underflow(parent, path[:-1])
            return

        # Merge with right sibling
        if node_idx < len(parent.children) - 1:
            right_sib = parent.children[node_idx + 1]
            parent_key = parent.keys.pop(node_idx)
            # Merge parent key and right sibling's keys/children into node
            node.keys.append(parent_key)
            node.keys.extend(right_sib.keys)
            node.children.extend(right_sib.children)
            parent.children.pop(node_idx + 1)

            if len(parent.keys) < 1:
                self._handle_internal_underflow(parent, path[:-1])
            return

    def save(self, force: bool = False):
        if not self.idx_file_path:
            return
        if not force:
            now = time.time()
            if now - self.last_save_time < 0.5:
                return
        self.last_save_time = time.time()
        # Serialize node structure to dict
        def serialize_node(node: BPlusNode):
            if node.is_leaf:
                return {
                    "is_leaf": True,
                    "keys": node.keys,
                    "values": node.children
                }
            else:
                return {
                    "is_leaf": False,
                    "keys": node.keys,
                    "children": [serialize_node(c) for c in node.children]
                }
        
        data = serialize_node(self.root)
        os.makedirs(os.path.dirname(self.idx_file_path), exist_ok=True)
        with open(self.idx_file_path, "w") as f:
            json.dump(data, f)

    def load(self):
        if not os.path.exists(self.idx_file_path):
            return

        with open(self.idx_file_path, "r") as f:
            try:
                data = json.load(f)
            except json.JSONDecodeError:
                return

        def deserialize_node(node_data):
            node = BPlusNode(is_leaf=node_data["is_leaf"])
            node.keys = node_data["keys"]
            if node.is_leaf:
                node.children = [tuple(v) if v is not None else None for v in node_data["values"]]
            else:
                node.children = [deserialize_node(c) for c in node_data["children"]]
            return node

        self.root = deserialize_node(data)
        
        # Link leaves and find head
        leaves = []
        def find_leaves(node: BPlusNode):
            if node.is_leaf:
                leaves.append(node)
            else:
                for child in node.children:
                    find_leaves(child)

        find_leaves(self.root)
        
        # Link them doubly
        for i in range(len(leaves)):
            if i > 0:
                leaves[i].prev = leaves[i - 1]
            if i < len(leaves) - 1:
                leaves[i].next = leaves[i + 1]
        
        if leaves:
            self.leaf_head = leaves[0]
        else:
            self.leaf_head = self.root
