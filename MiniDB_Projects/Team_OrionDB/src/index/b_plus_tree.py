import struct

class BPlusTree:
    MAX_KEYS = 4  # Small degree to trigger splits quickly for testing/demonstration
    INVALID_PAGE_ID = 0xFFFFFFFF

    def __init__(self, bpm, root_page_id=None):
        self.bpm = bpm
        if root_page_id is None or root_page_id == self.INVALID_PAGE_ID:
            # Create a new root page
            root_page = self.bpm.new_page()
            self.root_page_id = root_page.get_page_id()
            self._init_node(root_page, is_leaf=True, parent_page_id=self.INVALID_PAGE_ID)
            self.bpm.unpin_page(self.root_page_id, is_dirty=True)
        else:
            self.root_page_id = root_page_id

    # Node Initialization
    def _init_node(self, page, is_leaf, parent_page_id=INVALID_PAGE_ID):
        data = page.data
        # Offset 16: is_leaf (1 byte)
        struct.pack_into(">B", data, 16, 1 if is_leaf else 0)
        # Offset 17-18: num_keys (2 bytes)
        struct.pack_into(">H", data, 17, 0)
        # Offset 19-22: parent_page_id (4 bytes)
        struct.pack_into(">I", data, 19, parent_page_id)
        # Offset 23-26: next_page_id (4 bytes)
        struct.pack_into(">I", data, 23, self.INVALID_PAGE_ID)

    # Helper getters/setters on a Page object
    def _is_leaf(self, page):
        return struct.unpack_from(">B", page.data, 16)[0] == 1

    def _get_num_keys(self, page):
        return struct.unpack_from(">H", page.data, 17)[0]

    def _set_num_keys(self, page, num_keys):
        struct.pack_into(">H", page.data, 17, num_keys)

    def _get_parent(self, page):
        return struct.unpack_from(">I", page.data, 19)[0]

    def _set_parent(self, page, parent_page_id):
        struct.pack_into(">I", page.data, 19, parent_page_id)

    def _get_next(self, page):
        return struct.unpack_from(">I", page.data, 23)[0]

    def _set_next(self, page, next_page_id):
        struct.pack_into(">I", page.data, 23, next_page_id)

    # Key / Child / Value serialization layouts
    # Internal:
    # keys: offset 30 to 30 + MAX_KEYS*4
    # children: offset 30 + MAX_KEYS*4 to 30 + MAX_KEYS*8 + 4
    # Leaf:
    # keys: offset 30 to 30 + MAX_KEYS*4
    # values: offset 30 + MAX_KEYS*4 to 30 + MAX_KEYS*10
    
    # Key / Child / Value serialization layouts
    # Keys area: (MAX_KEYS + 1) entries to hold the overflow key during splits.
    # Internal:
    #   keys: offset 30 to 30 + (MAX_KEYS+1)*4
    #   children: offset 30 + (MAX_KEYS+1)*4 to 30 + (MAX_KEYS+1)*4 + (MAX_KEYS+2)*4
    # Leaf:
    #   keys: offset 30 to 30 + (MAX_KEYS+1)*4
    #   values: offset 30 + (MAX_KEYS+1)*4 to 30 + (MAX_KEYS+1)*4 + (MAX_KEYS+1)*6
    DATA_OFFSET = 30  # Start of key/child/value area (after node header)

    def _get_key(self, page, idx):
        offset = self.DATA_OFFSET + idx * 4
        return struct.unpack_from(">i", page.data, offset)[0]

    def _set_key(self, page, idx, key):
        offset = self.DATA_OFFSET + idx * 4
        struct.pack_into(">i", page.data, offset, key)

    def _get_child(self, page, idx):
        offset = self.DATA_OFFSET + (self.MAX_KEYS + 1) * 4 + idx * 4
        return struct.unpack_from(">I", page.data, offset)[0]

    def _set_child(self, page, idx, child_page_id):
        offset = self.DATA_OFFSET + (self.MAX_KEYS + 1) * 4 + idx * 4
        struct.pack_into(">I", page.data, offset, child_page_id)

    def _get_value(self, page, idx):
        # Value is (page_id: 4 bytes, slot_id: 2 bytes)
        offset = self.DATA_OFFSET + (self.MAX_KEYS + 1) * 4 + idx * 6
        pid, sid = struct.unpack_from(">IH", page.data, offset)
        return pid, sid

    def _set_value(self, page, idx, value):
        pid, sid = value
        offset = self.DATA_OFFSET + (self.MAX_KEYS + 1) * 4 + idx * 6
        struct.pack_into(">IH", page.data, offset, pid, sid)

    # Find the leaf page containing key
    def _find_leaf_page(self, key):
        curr_id = self.root_page_id
        path = []
        while True:
            page = self.bpm.fetch_page(curr_id)
            if self._is_leaf(page):
                self.bpm.unpin_page(curr_id, is_dirty=False)
                return curr_id, path
            
            # Internal node: find child
            num_keys = self._get_num_keys(page)
            child_idx = num_keys
            for i in range(num_keys):
                k = self._get_key(page, i)
                if key < k:
                    child_idx = i
                    break
            
            child_id = self._get_child(page, child_idx)
            self.bpm.unpin_page(curr_id, is_dirty=False)
            path.append(curr_id)
            curr_id = child_id

    # Search: returns (page_id, slot_id) or None
    def search(self, key):
        leaf_id, _ = self._find_leaf_page(key)
        leaf_page = self.bpm.fetch_page(leaf_id)
        num_keys = self._get_num_keys(leaf_page)
        
        result = None
        for i in range(num_keys):
            k = self._get_key(leaf_page, i)
            if k == key:
                result = self._get_value(leaf_page, i)
                break
                
        self.bpm.unpin_page(leaf_id, is_dirty=False)
        return result

    # Insert: entry point
    def insert(self, key, value):
        leaf_id, path = self._find_leaf_page(key)
        leaf_page = self.bpm.fetch_page(leaf_id)
        num_keys = self._get_num_keys(leaf_page)

        # Check if duplicate key already exists
        for i in range(num_keys):
            if self._get_key(leaf_page, i) == key:
                # Update existing value (upsert)
                self._set_value(leaf_page, i, value)
                self.bpm.unpin_page(leaf_id, is_dirty=True)
                return True

        # Insert in order
        insert_idx = num_keys
        for i in range(num_keys):
            if key < self._get_key(leaf_page, i):
                insert_idx = i
                break

        # Shift keys and values to the right
        for i in range(num_keys, insert_idx, -1):
            self._set_key(leaf_page, i, self._get_key(leaf_page, i - 1))
            self._set_value(leaf_page, i, self._get_value(leaf_page, i - 1))

        self._set_key(leaf_page, insert_idx, key)
        self._set_value(leaf_page, insert_idx, value)
        num_keys += 1
        self._set_num_keys(leaf_page, num_keys)

        if num_keys <= self.MAX_KEYS:
            self.bpm.unpin_page(leaf_id, is_dirty=True)
            return True

        # Split leaf node
        new_leaf = self.bpm.new_page()
        new_leaf_id = new_leaf.get_page_id()
        self._init_node(new_leaf, is_leaf=True, parent_page_id=self._get_parent(leaf_page))

        # Split point
        split_idx = num_keys // 2
        new_num_keys = num_keys - split_idx

        for i in range(new_num_keys):
            self._set_key(new_leaf, i, self._get_key(leaf_page, split_idx + i))
            self._set_value(new_leaf, i, self._get_value(leaf_page, split_idx + i))

        self._set_num_keys(leaf_page, split_idx)
        self._set_num_keys(new_leaf, new_num_keys)

        # Update sibling pointers
        self._set_next(new_leaf, self._get_next(leaf_page))
        self._set_next(leaf_page, new_leaf_id)

        split_key = self._get_key(new_leaf, 0)

        parent_id = self._get_parent(leaf_page)
        self.bpm.unpin_page(leaf_id, is_dirty=True)
        self.bpm.unpin_page(new_leaf_id, is_dirty=True)

        self._insert_in_parent(leaf_id, split_key, new_leaf_id, parent_id, path)
        return True

    def _insert_in_parent(self, left_id, key, right_id, parent_id, path):
        if parent_id == self.INVALID_PAGE_ID:
            # Create a new root page
            new_root = self.bpm.new_page()
            new_root_id = new_root.get_page_id()
            self._init_node(new_root, is_leaf=False, parent_page_id=self.INVALID_PAGE_ID)
            
            self._set_num_keys(new_root, 1)
            self._set_key(new_root, 0, key)
            self._set_child(new_root, 0, left_id)
            self._set_child(new_root, 1, right_id)

            # Update children's parent pointer
            l_page = self.bpm.fetch_page(left_id)
            self._set_parent(l_page, new_root_id)
            self.bpm.unpin_page(left_id, is_dirty=True)

            r_page = self.bpm.fetch_page(right_id)
            self._set_parent(r_page, new_root_id)
            self.bpm.unpin_page(right_id, is_dirty=True)

            self.root_page_id = new_root_id
            self.bpm.unpin_page(new_root_id, is_dirty=True)
            return

        # Insert key and child into existing parent
        parent = self.bpm.fetch_page(parent_id)
        num_keys = self._get_num_keys(parent)

        insert_idx = num_keys
        for i in range(num_keys):
            if key < self._get_key(parent, i):
                insert_idx = i
                break

        # Shift keys and children
        for i in range(num_keys, insert_idx, -1):
            self._set_key(parent, i, self._get_key(parent, i - 1))
            self._set_child(parent, i + 1, self._get_child(parent, i))

        self._set_key(parent, insert_idx, key)
        self._set_child(parent, insert_idx + 1, right_id)
        num_keys += 1
        self._set_num_keys(parent, num_keys)

        # Update parent pointer for right child
        r_page = self.bpm.fetch_page(right_id)
        self._set_parent(r_page, parent_id)
        self.bpm.unpin_page(right_id, is_dirty=True)

        if num_keys <= self.MAX_KEYS:
            self.bpm.unpin_page(parent_id, is_dirty=True)
            return

        # Split parent internal node
        new_parent = self.bpm.new_page()
        new_parent_id = new_parent.get_page_id()
        grandparent_id = path[-2] if len(path) >= 2 else self.INVALID_PAGE_ID
        self._init_node(new_parent, is_leaf=False, parent_page_id=grandparent_id)

        split_idx = num_keys // 2
        up_key = self._get_key(parent, split_idx)

        # Right child elements start from split_idx + 1
        new_num_keys = num_keys - (split_idx + 1)
        for i in range(new_num_keys):
            self._set_key(new_parent, i, self._get_key(parent, split_idx + 1 + i))
            self._set_child(new_parent, i, self._get_child(parent, split_idx + 1 + i))
        # Last child pointer
        self._set_child(new_parent, new_num_keys, self._get_child(parent, num_keys))

        self._set_num_keys(parent, split_idx)
        self._set_num_keys(new_parent, new_num_keys)

        # Update parent pointer for kids of the new parent node
        for i in range(new_num_keys + 1):
            kid_id = self._get_child(new_parent, i)
            kid_page = self.bpm.fetch_page(kid_id)
            self._set_parent(kid_page, new_parent_id)
            self.bpm.unpin_page(kid_id, is_dirty=True)

        self.bpm.unpin_page(parent_id, is_dirty=True)
        self.bpm.unpin_page(new_parent_id, is_dirty=True)

        if len(path) >= 1:
            path.pop()  # Pop current parent
        self._insert_in_parent(parent_id, up_key, new_parent_id, grandparent_id, path)

    # Delete key
    def delete(self, key):
        leaf_id, _ = self._find_leaf_page(key)
        leaf_page = self.bpm.fetch_page(leaf_id)
        num_keys = self._get_num_keys(leaf_page)

        delete_idx = -1
        for i in range(num_keys):
            if self._get_key(leaf_page, i) == key:
                delete_idx = i
                break

        if delete_idx == -1:
            self.bpm.unpin_page(leaf_id, is_dirty=False)
            return False

        # Shift keys and values to the left
        for i in range(delete_idx, num_keys - 1):
            self._set_key(leaf_page, i, self._get_key(leaf_page, i + 1))
            self._set_value(leaf_page, i, self._get_value(leaf_page, i + 1))

        self._set_num_keys(leaf_page, num_keys - 1)
        self.bpm.unpin_page(leaf_id, is_dirty=True)
        return True

    # Range search / scan helper: returns all values from key onwards
    def get_range(self, start_key=None, end_key=None):
        curr_id = self.root_page_id
        # Traverse to leftmost leaf or leaf containing start_key
        while True:
            page = self.bpm.fetch_page(curr_id)
            if self._is_leaf(page):
                self.bpm.unpin_page(curr_id, is_dirty=False)
                break
            
            if start_key is None:
                child_id = self._get_child(page, 0)
            else:
                num_keys = self._get_num_keys(page)
                child_idx = num_keys
                for i in range(num_keys):
                    if start_key < self._get_key(page, i):
                        child_idx = i
                        break
                child_id = self._get_child(page, child_idx)
            
            self.bpm.unpin_page(curr_id, is_dirty=False)
            curr_id = child_id

        # Scan leaves
        results = []
        leaf_id = curr_id
        while leaf_id != self.INVALID_PAGE_ID:
            leaf_page = self.bpm.fetch_page(leaf_id)
            num_keys = self._get_num_keys(leaf_page)
            for i in range(num_keys):
                k = self._get_key(leaf_page, i)
                if start_key is not None and k < start_key:
                    continue
                if end_key is not None and k > end_key:
                    self.bpm.unpin_page(leaf_id, is_dirty=False)
                    return results
                results.append((k, self._get_value(leaf_page, i)))
            
            next_leaf_id = self._get_next(leaf_page)
            self.bpm.unpin_page(leaf_id, is_dirty=False)
            leaf_id = next_leaf_id
            
        return results
