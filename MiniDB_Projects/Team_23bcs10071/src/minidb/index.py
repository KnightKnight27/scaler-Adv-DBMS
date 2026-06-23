class BPlusTreeNode:
    def __init__(self, is_leaf=False):
        self.is_leaf = is_leaf
        self.keys = []
        self.values = []  # child Node references if internal, RIDs (page_no, slot_id) if leaf
        self.next = None  # next leaf node link

class BPlusTree:
    def __init__(self, degree=3):
        self.root = BPlusTreeNode(is_leaf=True)
        self.degree = degree

    def search(self, key):
        node = self.root
        while not node.is_leaf:
            i = 0
            while i < len(node.keys) and key >= node.keys[i]:
                i += 1
            node = node.values[i]
        
        for i, k in enumerate(node.keys):
            if k == key:
                return node.values[i]
        return None

    def scan(self, low_key=None, high_key=None):
        node = self.root
        while not node.is_leaf:
            i = 0
            if low_key is not None:
                while i < len(node.keys) and low_key >= node.keys[i]:
                    i += 1
            node = node.values[i]
        
        results = []
        curr = node
        while curr:
            for i, k in enumerate(curr.keys):
                if low_key is not None and k < low_key:
                    continue
                if high_key is not None and k > high_key:
                    return results
                results.append((k, curr.values[i]))
            curr = curr.next
        return results

    def insert(self, key, value):
        node = self.root
        parent_stack = []
        while not node.is_leaf:
            parent_stack.append(node)
            i = 0
            while i < len(node.keys) and key >= node.keys[i]:
                i += 1
            node = node.values[i]

        i = 0
        while i < len(node.keys) and node.keys[i] < key:
            i += 1
        
        if i < len(node.keys) and node.keys[i] == key:
            # Overwrite duplicate key
            node.values[i] = value
            return

        node.keys.insert(i, key)
        node.values.insert(i, value)

        if len(node.keys) >= self.degree:
            self._split_leaf(node, parent_stack)

    def _split_leaf(self, leaf, parent_stack):
        mid = len(leaf.keys) // 2
        new_leaf = BPlusTreeNode(is_leaf=True)
        
        new_leaf.keys = leaf.keys[mid:]
        new_leaf.values = leaf.values[mid:]
        
        leaf.keys = leaf.keys[:mid]
        leaf.values = leaf.values[:mid]
        
        new_leaf.next = leaf.next
        leaf.next = new_leaf
        
        split_key = new_leaf.keys[0]
        self._insert_into_parent(leaf, split_key, new_leaf, parent_stack)

    def _insert_into_parent(self, child, key, new_child, parent_stack):
        if not parent_stack:
            new_root = BPlusTreeNode(is_leaf=False)
            new_root.keys = [key]
            new_root.values = [child, new_child]
            self.root = new_root
            return
        
        parent = parent_stack.pop()
        i = 0
        while i < len(parent.keys) and parent.keys[i] < key:
            i += 1
        
        parent.keys.insert(i, key)
        parent.values.insert(i + 1, new_child)
        
        if len(parent.keys) >= self.degree:
            self._split_internal(parent, parent_stack)

    def _split_internal(self, node, parent_stack):
        mid = len(node.keys) // 2
        split_key = node.keys[mid]
        
        new_node = BPlusTreeNode(is_leaf=False)
        new_node.keys = node.keys[mid + 1:]
        new_node.values = node.values[mid + 1:]
        
        node.keys = node.keys[:mid]
        node.values = node.values[:mid + 1]
        
        self._insert_into_parent(node, split_key, new_node, parent_stack)

    def delete(self, key):
        node = self.root
        while not node.is_leaf:
            i = 0
            while i < len(node.keys) and key >= node.keys[i]:
                i += 1
            node = node.values[i]
        
        if key in node.keys:
            idx = node.keys.index(key)
            node.keys.pop(idx)
            node.values.pop(idx)
