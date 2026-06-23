import sys

RED = 0
BLACK = 1

class Node:
    def __init__(self, key, color=RED):
        self.key = key
        self.color = color
        self.left = None
        self.right = None
        self.parent = None

    def __repr__(self):
        color_str = "RED" if self.color == RED else "BLACK"
        return f"Node({self.key}, {color_str})"

class RedBlackTree:
    def __init__(self):
        # NIL node representing leaves in Red-Black Trees
        self.NIL = Node(None, BLACK)
        self.root = self.NIL

    def left_rotate(self, x):
        y = x.right
        x.right = y.left
        if y.left != self.NIL:
            y.left.parent = x
        y.parent = x.parent
        if x.parent is None:
            self.root = y
        elif x == x.parent.left:
            x.parent.left = y
        else:
            x.parent.right = y
        y.left = x
        x.parent = y

    def right_rotate(self, x):
        y = x.left
        x.left = y.right
        if y.right != self.NIL:
            y.right.parent = x
        y.parent = x.parent
        if x.parent is None:
            self.root = y
        elif x == x.parent.right:
            x.parent.right = y
        else:
            x.parent.left = y
        y.right = x
        x.parent = y

    def insert(self, key):
        node = Node(key, RED)
        node.left = self.NIL
        node.right = self.NIL

        y = None
        x = self.root

        while x != self.NIL:
            y = x
            if node.key < x.key:
                x = x.left
            else:
                x = x.right

        node.parent = y
        if y is None:
            self.root = node
        elif node.key < y.key:
            y.left = node
        else:
            y.right = node

        if node.parent is None:
            node.color = BLACK
            return

        if node.parent.parent is None:
            return

        self.insert_fixup(node)

    def insert_fixup(self, k):
        while k.parent.color == RED:
            if k.parent == k.parent.parent.right:
                u = k.parent.parent.left
                if u.color == RED:
                    u.color = BLACK
                    k.parent.color = BLACK
                    k.parent.parent.color = RED
                    k = k.parent.parent
                else:
                    if k == k.parent.left:
                        k = k.parent
                        self.right_rotate(k)
                    k.parent.color = BLACK
                    k.parent.parent.color = RED
                    self.left_rotate(k.parent.parent)
            else:
                u = k.parent.parent.right
                if u.color == RED:
                    u.color = BLACK
                    k.parent.color = BLACK
                    k.parent.parent.color = RED
                    k = k.parent.parent
                else:
                    if k == k.parent.right:
                        k = k.parent
                        self.left_rotate(k)
                    k.parent.color = BLACK
                    k.parent.parent.color = RED
                    self.right_rotate(k.parent.parent)
            if k == self.root:
                break
        self.root.color = BLACK

    def dump_structure(self, node, level=0, prefix="Root"):
        if node == self.NIL:
            return
        
        # Output info for this node
        addr = f"0x{id(node):012X}"
        p_addr = f"0x{id(node.parent):012X}" if node.parent else "0x000000000000"
        l_addr = f"0x{id(node.left):012X}" if node.left != self.NIL else "0x000000000000 (NIL)"
        r_addr = f"0x{id(node.right):012X}" if node.right != self.NIL else "0x000000000000 (NIL)"
        color_str = "RED" if node.color == RED else "BLACK"
        
        print("  " * level + f"[{prefix}] Key: {node.key:3d} | Color: {color_str:5s} | NodeAddr: {addr} | ParentAddr: {p_addr} | LeftAddr: {l_addr} | RightAddr: {r_addr}")
        
        self.dump_structure(node.left, level + 1, "Left")
        self.dump_structure(node.right, level + 1, "Right")

def main():
    rbt = RedBlackTree()
    keys = [10, 20, 30, 15, 25]
    print(f"Inserting keys: {keys}\n")
    for key in keys:
        rbt.insert(key)

    print("="*100)
    print("RED-BLACK TREE MEMORY & POINTER DUMP")
    print("="*100)
    print(f"NIL Node Address: 0x{id(rbt.NIL):012X}")
    print(f"Root Address:     0x{id(rbt.root):012X}\n")
    
    rbt.dump_structure(rbt.root)
    print("="*100)

if __name__ == '__main__':
    main()